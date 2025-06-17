# Link Manager Architecture Analysis

## Обзор системы

Link Manager является центральным компонентом управления соединениями в Cellframe Node, который координирует взаимодействие между сетевым слоем, балансировщиком нагрузки и транспортным слоем (streams).

## Архитектурные компоненты

### 1. Core Link Manager (`dap_link_manager.c`)
- **Основные структуры**:
  - `dap_link_manager_t` - основная структура менеджера
  - `dap_link_t` - структура отдельного соединения
  - `dap_managed_net_t` - управляемая сеть

- **Hash-таблицы**:
  - `links` - хеш-таблица активных соединений по адресам узлов
  - Использует `uthash` для быстрого поиска

### 2. Network Layer Integration (`dap_chain_net.c`)
- **Callback механизм**:
  - `s_link_manager_callback_connected` - успешное соединение
  - `s_link_manager_callback_disconnected` - разрыв соединения  
  - `s_link_manager_callback_error` - ошибки соединения
  - `s_link_manager_fill_net_info` - заполнение информации о узле

### 3. Stream Layer Integration (`dap_stream.c`)
- **Проблема**: Отсутствуют объявления функций в заголовочных файлах
- **Функции интеграции**:
  - `dap_link_manager_stream_add()` - добавление нового потока
  - `dap_link_manager_stream_delete()` - удаление потока
  - `dap_link_manager_stream_replace()` - замена потока

## Выявленные проблемы

### 1. Отсутствующие объявления функций
**Проблема**: Stream layer использует неопределенные функции
```c
// В dap_stream.c отсутствуют объявления:
dap_link_manager_stream_add(&a_stream->node, a_stream->is_client_to_uplink);
dap_link_manager_stream_replace(&a_stream->node, l_stream->is_client_to_uplink);  
dap_link_manager_stream_delete(&a_stream->node);
```

### 2. Сложная многопоточная синхронизация
- Использование множественных pthread_rwlock
- Потенциальные deadlock'и при nested locking
- Callback'и выполняются в разных потоках

### 3. Глобальные состояния
- Одиночка `s_link_manager` - глобальное состояние
- Сложность тестирования и изоляции

### 4. Hot List механизм
- Использует Global DB для хранения "горячих" узлов
- 15-минутный период охлаждения  
- Префикс `local.nodes.heated.0x`

## Рекомендации по улучшению

### 1. Исправление объявлений функций
- Добавить объявления в `dap_link_manager.h`
- Обеспечить корректную видимость API

### 2. Упрощение синхронизации
- Рассмотреть использование single writer pattern
- Минимизировать nested locking
- Использовать message passing вместо shared state

### 3. Улучшение архитектуры
- Инверсия зависимостей через интерфейсы
- Dependency injection вместо глобальных состояний
- Разделение на более мелкие компоненты

### 4. Улучшение обработки ошибок
- Более четкие коды ошибок
- Centralized error handling
- Better logging and diagnostics

## Поток выполнения (State Machine)

```
DISCONNECTED -> CONNECTING -> ESTABLISHED -> DISCONNECTED
     ^                                            |
     |_____________ reconnect logic _______________|
```

## Критические секции

1. **Links hash table operations** - требуют write lock
2. **Managed nets list operations** - требуют nets lock  
3. **Global DB hot list updates** - могут блокировать
4. **Client connection state changes** - async callbacks

## Зависимости

- **dap-sdk**: core, crypto, io, global-db
- **cellframe-sdk**: net, cluster management
- **External**: pthread, uthash, http client

## Следующие шаги

1. Исправить проблемы с объявлениями функций
2. Провести рефакторинг синхронизации
3. Добавить юнит-тесты для критических путей
4. Документировать state machine переходы
5. Оптимизировать hot list mechanism 