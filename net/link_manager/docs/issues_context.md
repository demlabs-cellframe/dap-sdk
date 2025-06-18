# Link Manager Issues Context

## Критическая проблема: Отсутствующие объявления функций

### Локализация проблемы

**Файл**: `dap-sdk/net/stream/stream/dap_stream.c`
**Строки**: 955, 979, 982

```c
// Строка 955
dap_link_manager_stream_add(&a_stream->node, a_stream->is_client_to_uplink);

// Строка 979  
dap_link_manager_stream_replace(&a_stream->node, l_stream->is_client_to_uplink);

// Строка 982
dap_link_manager_stream_delete(&a_stream->node);
```

### Анализ вызовов

#### 1. `dap_link_manager_stream_add()` - строка 955
**Контекст**: функция `s_stream_add_to_hashtable()`
- Вызывается при добавлении потока в hash table
- Передаются: адрес узла и флаг uplink/downlink
- **Назначение**: уведомить link manager о новом активном потоке

#### 2. `dap_link_manager_stream_replace()` - строка 979
**Контекст**: функция `s_stream_delete_from_list()`
- Вызывается при замене потока в списке
- Логика: если найден другой поток с тем же адресом, заменить направление
- **Назначение**: обновить информацию о направлении связи (uplink/downlink)

#### 3. `dap_link_manager_stream_delete()` - строка 982
**Контекст**: функция `s_stream_delete_from_list()`
- Вызывается при удалении потока из кластера
- **Назначение**: уведомить link manager об удалении потока

## Реализация в Link Manager

### Найденные функции в `dap_link_manager.c`:

```c
// Строка 1152 - соответствует stream_add
int dap_link_manager_stream_add(dap_stream_node_addr_t *a_node_addr, bool a_uplink)

// Строка 1212 - соответствует stream_replace  
void dap_link_manager_stream_replace(dap_stream_node_addr_t *a_addr, bool a_new_is_uplink)

// Строка 1233 - соответствует stream_delete
void dap_link_manager_stream_delete(dap_stream_node_addr_t *a_node_addr)
```

## Вторичные проблемы архитектуры

### 1. Сложная логика состояний соединений

**Проблемное место**: функция `s_link_drop()` (строка 581)
```c
void s_link_drop(dap_link_t *a_link, bool a_disconnected)
```

**Проблемы**:
- Сложная логика с множественными условиями
- Nested callbacks в многопоточной среде
- Потенциальные race conditions

### 2. Глобальные блокировки

**Проблемное место**: использование `pthread_rwlock` в нескольких местах
```c
pthread_rwlock_wrlock(&s_link_manager->links_lock);  // Множественные места
pthread_rwlock_wrlock(&s_link_manager->nets_lock);   // Множественные места
```

**Риски**:
- Потенциальные deadlock'и
- Снижение производительности из-за блокировок
- Сложность отладки многопоточных проблем

### 3. Hot List механизм

**Проблемное место**: функции работы с "горячими" узлами
```c
static int s_update_hot_list(uint64_t a_net_id)           // Строка 94
static void s_node_hot_list_add(...)                     // Строка 115
static char *s_hot_group_forming(uint64_t a_net_id)      // Строка 86
```

**Проблемы**:
- Использование Global DB для временного состояния
- Фиксированный период охлаждения (15 минут)
- Потенциальная утечка записей при сбоях

### 4. Сложный callback механизм

**Проблемное место**: структура callbacks в Chain Net
```c
// В dap_chain_net.c
static void s_link_manager_callback_connected(dap_link_t *a_link, uint64_t a_net_id);
static void s_link_manager_callback_error(dap_link_t *a_link, uint64_t a_net_id, int a_error);  
static bool s_link_manager_callback_disconnected(dap_link_t *a_link, uint64_t a_net_id, int a_links_count);
```

**Проблемы**:
- Callbacks выполняются в разных потоках
- Сложная логика принятия решений о reconnect
- Отсутствие centralized error handling

## Приоритетность исправлений

### Критичность: Высокая
1. **Объявления функций** - блокирует компиляцию
2. **Race conditions в link state management** - stability issues

### Критичность: Средняя  
3. **Упрощение блокировок** - performance optimization
4. **Hot list optimization** - resource usage

### Критичность: Низкая
5. **Callback architecture refactoring** - code maintainability
6. **Global state elimination** - testability

## Зависимости между модулями

```
Stream Layer -> Link Manager -> Chain Net -> Balancer
     ^                                          |
     |_________ Indirect dependency ____________|
```

**Проблема**: Circular dependencies затрудняют рефакторинг

## Рекомендуемый план исправлений

### Фаза 1: Критические исправления (1-2 дня)
1. Добавить объявления функций в заголовочный файл
2. Исправить компиляционные ошибки
3. Базовое тестирование интеграции

### Фаза 2: Стабилизация (1 неделя)  
1. Анализ race conditions
2. Добавление defensive programming
3. Улучшение логирования для отладки

### Фаза 3: Оптимизация (2-3 недели)
1. Рефакторинг синхронизации
2. Оптимизация hot list механизма
3. Упрощение callback архитектуры

## Тестовые сценарии

### Для проверки исправлений:
1. **Connection lifecycle**: create -> connect -> disconnect -> cleanup
2. **Multiple networks**: параллельная работа нескольких сетей
3. **High load**: множественные одновременные соединения  
4. **Error conditions**: сетевые сбои, таймауты, invalid data
5. **Hot list behavior**: добавление/удаление узлов, период охлаждения 