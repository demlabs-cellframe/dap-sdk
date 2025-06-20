# HTTP2 Client Architecture: Stream Manager Integration

## Интеграция с Reactor архитектурой

Менеджер стримов интегрируется с существующей `dap_events_socket_t` и `dap_worker_t` архитектурой для обеспечения эффективного управления автономными стримами в многопоточной среде.

## Архитектура интеграции

```
┌─────────────────────────────────────────────────────────────┐
│                    APPLICATION LAYER                       │
│  ┌─────────────────┐ ┌─────────────────┐ ┌───────────────┐  │
│  │ HTTP Client     │ │ WebSocket App   │ │ HTTP Server   │  │
│  │ API             │ │ Chat/SSE        │ │ API           │  │
│  └─────────────────┘ └─────────────────┘ └───────────────┘  │
└─────────────────────────────────────────────────────────────┘
                               │
┌─────────────────────────────────────────────────────────────┐
│                  STREAM MANAGER LAYER                      │
│  ┌─────────────────────────────────────────────────────────┐  │
│  │         dap_http2_stream_manager_t                      │  │
│  │  • Global stream registry                              │  │
│  │  • Cross-worker stream lookup                          │  │
│  │  • Autonomous stream lifecycle                         │  │
│  │  • Load balancing & statistics                         │  │
│  └─────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
                               │
┌─────────────────────────────────────────────────────────────┐
│                    REACTOR LAYER                           │
│  ┌───────────────┐ ┌───────────────┐ ┌─────────────────────┐  │
│  │ dap_worker_t  │ │ dap_worker_t  │ │ dap_worker_t        │  │
│  │ #0            │ │ #1            │ │ #N                  │  │
│  │               │ │               │ │                     │  │
│  │ ┌───────────┐ │ │ ┌───────────┐ │ │ ┌─────────────────┐ │  │
│  │ │dap_events_│ │ │ │dap_events_│ │ │ │dap_events_      │ │  │
│  │ │socket_t   │ │ │ │socket_t   │ │ │ │socket_t         │ │  │
│  │ │           │ │ │ │           │ │ │ │                 │ │  │
│  │ │ Stream A  │ │ │ │ Stream B  │ │ │ Stream C        │ │  │
│  │ │ Stream D  │ │ │ │ Stream E  │ │ │ Stream F        │ │  │
│  │ └───────────┘ │ │ └───────────┘ │ │ └─────────────────┘ │  │
│  └───────────────┘ └───────────────┘ └─────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

## Stream Manager структура

```c
typedef struct dap_http2_stream_manager {
    // === Глобальный реестр стримов ===
    dap_http2_stream_t **streams;           // Массив всех автономных стримов
    size_t streams_count;                   // Количество активных стримов
    size_t streams_capacity;                // Размер массива
    pthread_rwlock_t streams_lock;          // RW блокировка для массива
    
    // === UUID → Worker mapping ===
    typedef struct {
        dap_stream_uuid_t uuid;
        uint16_t worker_id;
        uint32_t esocket_uuid;              // Для быстрого поиска
    } stream_location_t;
    
    stream_location_t *locations;           // Хеш-таблица локаций
    size_t locations_count;
    pthread_rwlock_t locations_lock;
    
    // === Per-Worker кеши ===
    typedef struct {
        dap_http2_stream_t **local_streams; // Локальные стримы worker'а
        size_t local_count;
        size_t local_capacity;
        pthread_mutex_t local_lock;         // Быстрая блокировка для локального кеша
    } worker_cache_t;
    
    worker_cache_t *worker_caches;          // Массив кешей по worker'ам
    size_t workers_count;
    
    // === Статистика ===
    struct {
        _Atomic uint64_t total_streams_created;
        _Atomic uint64_t total_streams_destroyed;
        _Atomic uint64_t active_websocket_streams;
        _Atomic uint64_t active_sse_streams;
        _Atomic uint64_t cross_worker_lookups;
        _Atomic uint64_t cache_hits;
        _Atomic uint64_t cache_misses;
    } stats;
    
    // === Конфигурация ===
    size_t max_streams_per_worker;
    size_t max_total_streams;
    uint64_t cleanup_interval_ms;
    
    // === Cleanup timer ===
    dap_timerfd_t *cleanup_timer;
    
} dap_http2_stream_manager_t;
```

## Composite Stream UID

```c
// 64-битный упакованный UID для atomic операций
typedef union dap_stream_uid {
    _Atomic uint64_t atomic_value;
    struct {
        uint16_t stream_id;         // 0-65535 стримов на сокет
        uint32_t esocket_uuid;      // UUID сокета (урезанный до 32-бит)
        uint16_t worker_id;         // 0-65535 worker'ов
    } parts;
} dap_stream_uid_t;

// Генерация уникального UID
static inline dap_stream_uid_t dap_stream_uid_generate(dap_events_socket_t *es) {
    static _Atomic uint16_t stream_counter = 0;
    
    dap_stream_uid_t uid = {0};
    uid.parts.stream_id = atomic_fetch_add(&stream_counter, 1);
    uid.parts.esocket_uuid = (uint32_t)(es->uuid & 0xFFFFFFFF);  // Младшие 32 бита
    uid.parts.worker_id = es->worker->id;
    
    return uid;
}
```

## Интеграция с dap_events_socket_t

### **Расширение events socket для стримов**
```c
// Добавляем поля в dap_events_socket_t (или создаем extension)
typedef struct dap_events_socket_stream_ext {
    dap_http2_stream_t **streams;           // Стримы привязанные к этому сокету
    size_t streams_count;
    size_t streams_capacity;
    pthread_mutex_t streams_lock;
    
    // Callbacks для stream events
    void (*stream_created)(dap_events_socket_t *es, dap_http2_stream_t *stream);
    void (*stream_destroyed)(dap_events_socket_t *es, dap_http2_stream_t *stream);
    void (*stream_detached)(dap_events_socket_t *es, dap_http2_stream_t *stream);
} dap_events_socket_stream_ext_t;

// Привязка extension к events socket
int dap_events_socket_enable_streams(dap_events_socket_t *es) {
    if (es->_stream_ext)
        return 0;  // Уже включено
        
    es->_stream_ext = DAP_NEW_Z(dap_events_socket_stream_ext_t);
    if (!es->_stream_ext)
        return -ENOMEM;
        
    pthread_mutex_init(&es->_stream_ext->streams_lock, NULL);
    return 0;
}
```

### **Создание стрима на events socket**
```c
dap_http2_stream_t *dap_events_socket_create_stream(dap_events_socket_t *es, 
                                                   dap_http2_session_t *session) {
    if (!es->_stream_ext) {
        if (dap_events_socket_enable_streams(es) != 0)
            return NULL;
    }
    
    dap_http2_stream_t *stream = dap_http2_stream_create(session);
    if (!stream)
        return NULL;
        
    // Генерируем уникальный UID
    stream->uuid = dap_stream_uid_generate(es);
    
    // Добавляем в локальный список сокета
    pthread_mutex_lock(&es->_stream_ext->streams_lock);
    // Расширяем массив при необходимости
    if (es->_stream_ext->streams_count >= es->_stream_ext->streams_capacity) {
        size_t new_capacity = es->_stream_ext->streams_capacity ? 
                             es->_stream_ext->streams_capacity * 2 : 4;
        dap_http2_stream_t **new_streams = DAP_REALLOC(es->_stream_ext->streams, 
                                                       new_capacity * sizeof(void*));
        if (!new_streams) {
            pthread_mutex_unlock(&es->_stream_ext->streams_lock);
            dap_http2_stream_delete(stream);
            return NULL;
        }
        es->_stream_ext->streams = new_streams;
        es->_stream_ext->streams_capacity = new_capacity;
    }
    
    es->_stream_ext->streams[es->_stream_ext->streams_count++] = stream;
    pthread_mutex_unlock(&es->_stream_ext->streams_lock);
    
    // Уведомляем callback
    if (es->_stream_ext->stream_created)
        es->_stream_ext->stream_created(es, stream);
        
    return stream;
}
```

## Регистрация автономных стримов

### **Отстёгивание стрима от клиента**
```c
int dap_http2_stream_detach_from_client(dap_http2_stream_t *stream) {
    dap_return_val_if_fail(stream && stream->session, -EINVAL);
    
    dap_events_socket_t *es = stream->session->es;
    dap_http2_stream_manager_t *manager = dap_http2_stream_manager_get_global();
    
    // 1. Регистрируем в глобальном менеджере
    int ret = dap_http2_stream_manager_register(manager, stream);
    if (ret != 0) {
        log_it(L_ERROR, "Failed to register stream "DAP_FORMAT_STREAM_UUID" in global manager", 
               stream->uuid.atomic_value);
        return ret;
    }
    
    // 2. Помечаем как автономный
    stream->flags |= DAP_STREAM_FLAG_AUTONOMOUS;
    stream->detached_timestamp = time(NULL);
    
    // 3. Уведомляем events socket
    if (es->_stream_ext && es->_stream_ext->stream_detached)
        es->_stream_ext->stream_detached(es, stream);
        
    // 4. Обновляем статистику
    atomic_fetch_add(&manager->stats.active_websocket_streams, 1);
    
    log_it(L_INFO, "Stream "DAP_FORMAT_STREAM_UUID" detached and registered as autonomous", 
           stream->uuid.atomic_value);
    return 0;
}
```

## Cross-Worker Stream Lookup

### **Поиск стрима по UUID**
```c
dap_http2_stream_t *dap_http2_stream_manager_find(dap_http2_stream_manager_t *manager, 
                                                  dap_stream_uid_t uuid) {
    dap_return_val_if_fail(manager, NULL);
    
    // 1. Быстрая проверка в локальном кеше текущего worker'а
    dap_worker_t *current_worker = dap_worker_get_current();
    if (current_worker && current_worker->id < manager->workers_count) {
        worker_cache_t *cache = &manager->worker_caches[current_worker->id];
        pthread_mutex_lock(&cache->local_lock);
        
        for (size_t i = 0; i < cache->local_count; i++) {
            if (cache->local_streams[i]->uuid.atomic_value == uuid.atomic_value) {
                dap_http2_stream_t *stream = cache->local_streams[i];
                pthread_mutex_unlock(&cache->local_lock);
                atomic_fetch_add(&manager->stats.cache_hits, 1);
                return stream;
            }
        }
        pthread_mutex_unlock(&cache->local_lock);
    }
    
    // 2. Поиск в таблице локаций для определения worker'а
    uint16_t target_worker_id = UINT16_MAX;
    pthread_rwlock_rdlock(&manager->locations_lock);
    
    for (size_t i = 0; i < manager->locations_count; i++) {
        if (manager->locations[i].uuid.atomic_value == uuid.atomic_value) {
            target_worker_id = manager->locations[i].worker_id;
            break;
        }
    }
    pthread_rwlock_unlock(&manager->locations_lock);
    
    if (target_worker_id == UINT16_MAX) {
        atomic_fetch_add(&manager->stats.cache_misses, 1);
        return NULL;  // Стрим не найден
    }
    
    // 3. Поиск в кеше целевого worker'а
    if (target_worker_id < manager->workers_count) {
        worker_cache_t *target_cache = &manager->worker_caches[target_worker_id];
        pthread_mutex_lock(&target_cache->local_lock);
        
        for (size_t i = 0; i < target_cache->local_count; i++) {
            if (target_cache->local_streams[i]->uuid.atomic_value == uuid.atomic_value) {
                dap_http2_stream_t *stream = target_cache->local_streams[i];
                pthread_mutex_unlock(&target_cache->local_lock);
                atomic_fetch_add(&manager->stats.cross_worker_lookups, 1);
                return stream;
            }
        }
        pthread_mutex_unlock(&target_cache->local_lock);
    }
    
    atomic_fetch_add(&manager->stats.cache_misses, 1);
    return NULL;
}
```

## Заключение

Интеграция Stream Manager с reactor архитектурой обеспечивает:

- **Эффективность**: per-worker кеши минимизируют блокировки
- **Масштабируемость**: поддержка тысяч автономных стримов
- **Надежность**: автоматическая очистка и управление жизненным циклом
- **Мониторинг**: детальная статистика и метрики
- **Совместимость**: полная интеграция с существующей архитектурой

Этот подход позволяет эффективно управлять большим количеством долгоживущих соединений (WebSocket, SSE) в высоконагруженных приложениях. 