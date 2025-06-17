# Link Manager API Specification

## Public API Functions

### Initialization & Configuration

```c
// Инициализация Link Manager с callbacks
int dap_link_manager_init(const dap_link_manager_callbacks_t *a_callbacks);

// Деинициализация и очистка ресурсов  
void dap_link_manager_deinit();

// Создание нового экземпляра Link Manager
dap_link_manager_t *dap_link_manager_new(const dap_link_manager_callbacks_t *a_callbacks);

// Получение экземпляра по умолчанию
dap_link_manager_t *dap_link_manager_get_default();
```

### Network Management

```c
// Добавление управляемой сети
int dap_link_manager_add_net(uint64_t a_net_id, dap_cluster_t *a_link_cluster, uint32_t a_min_links_number);

// Ассоциация дополнительного кластера с сетью
int dap_link_manager_add_net_associate(uint64_t a_net_id, dap_cluster_t *a_link_cluster);

// Удаление сети из управления
void dap_link_manager_remove_net(uint64_t a_net_id);

// Установка состояния сети (активна/неактивна)
void dap_link_manager_set_net_condition(uint64_t a_net_id, bool a_new_condition);

// Получение состояния сети
bool dap_link_manager_get_net_condition(uint64_t a_net_id);
```

### Link Statistics & Information

```c
// Подсчет активных соединений в сети
size_t dap_link_manager_links_count(uint64_t a_net_id);

// Получение требуемого количества соединений
size_t dap_link_manager_required_links_count(uint64_t a_net_id);

// Подсчет недостающих соединений
size_t dap_link_manager_needed_links_count(uint64_t a_net_id);

// Получение списка адресов соединений для сети
dap_stream_node_addr_t *dap_link_manager_get_net_links_addrs(
    uint64_t a_net_id, 
    size_t *a_uplinks_count, 
    size_t *a_downlinks_count, 
    bool a_established_only
);

// Получение списка игнорируемых адресов (hot list)
dap_stream_node_addr_t *dap_link_manager_get_ignored_addrs(size_t *a_ignored_count, uint64_t a_net_id);
```

### Link Operations

```c
// Создание нового соединения
int dap_link_manager_link_create(dap_stream_node_addr_t *a_node_addr, uint64_t a_associated_net_id);

// Обновление информации о соединении
int dap_link_manager_link_update(dap_stream_node_addr_t *a_node_addr, const char *a_host, uint16_t a_port);

// Поиск соединения
bool dap_link_manager_link_find(dap_stream_node_addr_t *a_node_addr, uint64_t a_net_id);
```

### Stream Integration

```c
// Уведомление о добавлении потока
int dap_link_manager_stream_add(dap_stream_node_addr_t *a_node_addr, bool a_uplink);

// Уведомление о замене потока  
void dap_link_manager_stream_replace(dap_stream_node_addr_t *a_addr, bool a_new_is_uplink);

// Уведомление об удалении потока
void dap_link_manager_stream_delete(dap_stream_node_addr_t *a_node_addr);
```

### Cluster Management

```c
// Callback для добавления в links cluster
void dap_link_manager_add_links_cluster(dap_cluster_member_t *a_member, void *a_arg);

// Callback для удаления из links cluster  
void dap_link_manager_remove_links_cluster(dap_cluster_member_t *a_member, void *a_arg);

// Callback для добавления в static cluster
void dap_link_manager_add_static_links_cluster(dap_cluster_member_t *a_member, void *a_arg);

// Callback для удаления из static cluster
void dap_link_manager_remove_static_links_cluster(dap_cluster_member_t *a_member, void *a_arg);
```

### Network Accounting

```c
// Учет соединения в сети
void dap_link_manager_accounting_link_in_net(uint64_t a_net_id, dap_stream_node_addr_t *a_node_addr, bool a_no_error);
```

### Control Functions

```c
// Установка активности Link Manager
void dap_link_manager_set_condition(bool a_new_condition);

// Получение состояния активности
bool dap_link_manager_get_condition();
```

## Callback Interface

### Required Callbacks Structure

```c
typedef struct dap_link_manager_callbacks {
    // Заполнение информации о сети для соединения (ОБЯЗАТЕЛЬНЫЙ)
    int (*fill_net_info)(dap_link_t *a_link);
    
    // Запрос новых соединений (опциональный)
    int (*link_request)(uint64_t a_net_id);
    
    // Уведомление об установлении соединения (опциональный)
    void (*connected)(dap_link_t *a_link, uint64_t a_net_id);
    
    // Уведомление об ошибке соединения (опциональный)  
    void (*error)(dap_link_t *a_link, uint64_t a_net_id, int a_error);
    
    // Уведомление о разрыве соединения с возможностью сохранения (опциональный)
    bool (*disconnected)(dap_link_t *a_link, uint64_t a_net_id, int a_links_count);
    
    // Уведомление об изменении количества соединений (опциональный)
    int (*link_count_changed)();
} dap_link_manager_callbacks_t;
```

## Data Structures

### Link Structure
```c
typedef struct dap_link {
    dap_stream_node_addr_t addr;           // Адрес узла
    bool is_uplink;                        // Направление соединения
    bool stream_is_destroyed;              // Флаг разрушенного потока
    
    dap_link_manager_t *link_manager;      // Обратная ссылка на менеджер
    
    // Списки кластеров
    dap_list_t *active_clusters;           // Активные кластеры
    dap_list_t *static_clusters;           // Статические кластеры
    
    // Uplink данные
    struct {
        dap_client_t *client;              // Клиент для исходящих соединений
        dap_events_socket_uuid_t es_uuid;  // UUID события сокета
        dap_link_state_t state;            // Состояние соединения
        dap_time_t start_after;            // Время следующей попытки
        uint32_t attempts_count;           // Счетчик попыток
        bool ready;                        // Готовность к соединению
        dap_list_t *associated_nets;       // Ассоциированные сети
    } uplink;
    
    UT_hash_handle hh;                     // Handle для hash table
} dap_link_t;
```

### Manager Structure
```c
typedef struct dap_link_manager {
    bool active;                           // Активность менеджера
    
    dap_link_t *links;                     // Hash table соединений
    pthread_rwlock_t links_lock;           // Блокировка соединений
    
    dap_list_t *nets;                      // Список управляемых сетей
    pthread_rwlock_t nets_lock;            // Блокировка сетей
    
    dap_link_manager_callbacks_t callbacks; // Callback'и
    
    uint32_t max_attempts_num;             // Макс. попыток соединения
    uint32_t reconnect_delay;              // Задержка переподключения
} dap_link_manager_t;
```

## Error Codes

### Function Return Values
- `0` - Success
- `-1` - Invalid arguments
- `-2` - Initialization error / Already exists
- `-3` - Manager not initialized
- `-4` - Already associated
- `-5` - Invalid node address
- `-6` - Invalid network address
- `-7` - Memory allocation error

### Link States
```c
typedef enum {
    LINK_STATE_DISCONNECTED = 0,  // Отключено
    LINK_STATE_CONNECTING,        // Подключается
    LINK_STATE_ESTABLISHED        // Установлено
} dap_link_state_t;
```

## Configuration Parameters

### Config Section: `[link_manager]`
- `timer_update_states` - интервал обновления состояний (мс, по умолчанию: 5000)
- `max_attempts_num` - максимальное количество попыток (по умолчанию: 1)  
- `reconnect_delay` - задержка переподключения (сек, по умолчанию: 20)
- `debug_more` - расширенная отладка (bool, по умолчанию: false)

## Threading Model

### Thread Safety
- **Read operations**: thread-safe с read locks
- **Write operations**: требуют write locks
- **Callbacks**: выполняются в query thread
- **State updates**: через timer callbacks

### Lock Hierarchy
1. `nets_lock` - блокировка списка сетей (высший уровень)
2. `links_lock` - блокировка hash table соединений
3. Cluster locks - внутренние блокировки кластеров

**⚠️ Важно**: всегда захватывать блокировки в указанном порядке для избежания deadlock'ов.

## Hot List Mechanism

### Purpose
Предотвращение частых переподключений к "проблемным" узлам

### Implementation
- **Storage**: Global DB groups с префиксом `local.nodes.heated.0x`
- **Cooling period**: 15 минут (900 секунд)
- **Cleanup**: автоматическая очистка по таймеру

### Functions
```c
static int s_update_hot_list(uint64_t a_net_id);           // Очистка устаревших записей
static void s_node_hot_list_add(...);                     // Добавление узла в hot list
static char *s_hot_group_forming(uint64_t a_net_id);      // Формирование имени группы
``` 