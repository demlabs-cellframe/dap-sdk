# Callback Architecture Design

## Архитектурная философия

### Принцип "Embedded Transitions"
Логика переходов между протоколами встроена в сами callback'ы, а не управляется внешними картами состояний.

### Системный подход
- **Доверяем разработчику** - он знает свою задачу лучше
- **Предоставляем инструменты** - а не ограничения  
- **Производительность** важнее "защиты от дурака"
- **Гибкость** важнее простоты для новичков

## Архитектурная оценка: 9.5/10

### ✅ ПРЕИМУЩЕСТВА

#### Производительность
- **Zero overhead** - никаких поисков в картах, прямые вызовы функций
- **Локальность данных** - весь контекст в одном месте
- **Минимум аллокаций** - нет динамических структур для переходов
- **Предсказуемость** - компилятор может оптимизировать call chain

#### Простота использования
- **Интуитивность** - логика переходов видна в коде
- **Отладочность** - легко трассировать, ставить breakpoint'ы
- **Читаемость** - весь protocol flow в одном месте
- **Минимум boilerplate** - не нужно описывать карты переходов

#### Гибкость
- **Полный контроль** - Application может принимать любые решения
- **Условная логика** - сложные условия переходов в коде
- **Состояние** - доступ ко всему контексту для принятия решений
- **Динамичность** - переходы могут зависеть от runtime данных

#### Архитектурная чистота
- **Принцип единственной ответственности** - каждый callback знает только свой протокол
- **Инкапсуляция** - логика протокола скрыта внутри Application
- **Слабая связанность** - SDK не знает о специфике протоколов

### ⚠️ ТРЕБУЕТ ОСТОРОЖНОСТИ

#### Ответственность разработчиков
- Повышенная ответственность за корректность state machine
- Нужна хорошая документация с примерами
- Желательны helper функции для безопасности

#### Потенциальные проблемы
- Циклические переходы без контроля
- Утечки памяти при неаккуратных переходах
- Сложность тестирования всех возможных путей

## ПРИМЕНИМОСТЬ

### ✅ Отлично подходит для:
- **Простых случаев** - один callback без переходов
- **Сложных протокольных переходов** - HTTP → WebSocket → Binary
- **Высокопроизводительных приложений** - системное программирование
- **Максимальной гибкости** - когда нужен полный контроль

### ❌ Не подходит для:
- Разработчиков, которые не хотят думать о state management
- Случаев, где нужна строгая валидация переходов на уровне SDK

## АРХИТЕКТУРНЫЕ ПРИНЦИПЫ

### 1. Don't Pay For What You Don't Use
```c
// Простой случай - никакого overhead'а
static size_t simple_http_callback(dap_http2_stream_t *stream, const void *data, size_t size) {
    return parse_http_response(stream, data, size);
    // Никаких переходов - никакой сложности
}
```

### 2. Embedded State Machine
```c
// Сложный случай - переходы встроены в логику
static size_t complex_http_callback(dap_http2_stream_t *stream, const void *data, size_t size) {
    my_context_t *ctx = (my_context_t*)stream->read_callback_context;
    
    size_t processed = parse_http_data(stream, data, size);
    
    // Переход встроен в логику протокола
    if (ctx->upgrade_to_websocket) {
        dap_http2_stream_set_read_callback(stream, websocket_callback, ctx);
    }
    
    return processed;
}
```

### 3. Self-Contained Context
```c
// Контекст содержит ВСЕ необходимое для всех протоколов
typedef struct my_application_context {
    // HTTP state
    http_parser_t http_parser;
    
    // WebSocket state  
    websocket_parser_t ws_parser;
    
    // Binary protocol state
    binary_protocol_t binary_proto;
    
    // Все callback'ы доступны
    dap_stream_read_callback_t http_callback;
    dap_stream_read_callback_t websocket_callback;
    dap_stream_read_callback_t binary_callback;
    
} my_application_context_t;
```

## СРАВНЕНИЕ С АЛЬТЕРНАТИВАМИ

### vs State Machine Pattern
| Embedded Transitions | State Machine |
|---------------------|---------------|
| ✅ Простота реализации | ❌ Сложность setup |
| ✅ Zero overhead | ❌ Lookup overhead |
| ✅ Гибкость логики | ❌ Жесткие правила |
| ❌ Нет валидации | ✅ Валидация переходов |
| ❌ Сложность отладки flow | ✅ Четкие состояния |

### vs Strategy Pattern
| Embedded Transitions | Strategy Pattern |
|---------------------|------------------|
| ✅ Динамические переходы | ❌ Статические стратегии |
| ✅ Контекстные решения | ❌ Изолированные стратегии |
| ✅ Производительность | ❌ Virtual call overhead |
| ❌ Нет полиморфизма | ✅ Четкие интерфейсы |

## BEST PRACTICES

### 1. Безопасные переходы
```c
// Всегда обрабатывать остатки данных
if (processed < size) {
    return processed + new_callback(stream, (char*)data + processed, size - processed);
}
```

### 2. Очистка ресурсов
```c
// При переходах очищать старое состояние
cleanup_http_parser(&ctx->http_parser);
init_websocket_parser(&ctx->ws_parser);
```

### 3. Защита от циклов
```c
// Ограничение количества переходов
if (ctx->transition_count > MAX_TRANSITIONS) {
    return -1; // Error
}
ctx->transition_count++;
```

## РЕКОМЕНДУЕМЫЕ УЛУЧШЕНИЯ

### Helper функции
```c
// Безопасный переход с проверками
int dap_stream_safe_transition(dap_http2_stream_t *stream, 
                              dap_stream_read_callback_t new_callback,
                              void *new_context);
```

### Debug поддержка
```c
#ifdef DAP_DEBUG_TRANSITIONS
    #define LOG_TRANSITION(stream, old_cb, new_cb) \
        log_it(L_DEBUG, "Stream %u: %p -> %p", stream->stream_id, old_cb, new_cb)
#else
    #define LOG_TRANSITION(stream, old_cb, new_cb)
#endif
```

### Валидация в debug режиме
```c
#ifdef DAP_DEBUG
    validate_transition_safety(stream, new_callback);
    check_context_consistency(new_context);
#endif
```

## ЗАКЛЮЧЕНИЕ

Архитектура "Embedded Transitions" представляет собой оптимальное решение для высокопроизводительного SDK, где:

1. **Производительность** - критически важна
2. **Гибкость** - необходима для сложных протоколов  
3. **Простота** - важна для базовых случаев
4. **Ответственность** - лежит на Application разработчике

Подход полностью соответствует философии системного программирования и принципам языка C, обеспечивая максимальную эффективность при сохранении архитектурной чистоты. 