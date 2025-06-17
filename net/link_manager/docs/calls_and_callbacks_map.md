# Link Manager Calls & Callbacks Map

## 🎯 Цель документа

Полная карта всех API вызовов и колбеков Link Manager модуля для детального анализа threading refactoring. Показывает где, зачем и как используется каждая функция.

## 📊 Диаграмма взаимодействий

```
┌─────────────┐    ┌─────────────┐    ┌─────────────┐    ┌─────────────┐
│Chain Net    │    │Stream Layer │    │Balancer     │    │Consensus    │
│Layer        │    │             │    │             │    │Layer        │
└─────┬───────┘    └─────┬───────┘    └─────┬───────┘    └─────┬───────┘
      │                  │                  │                  │
      │ ┌────────────────┼──────────────────┼──────────────────┼─────┐
      │ │                │                  │                  │     │
      ▼ ▼                ▼                  ▼                  ▼     │
┌─────────────────────────────────────────────────────────────────────┐ │
│                    LINK MANAGER CORE                               │ │
│  ┌─────────────┐ ┌─────────────┐ ┌─────────────┐ ┌─────────────┐   │ │
│  │Network Mgmt │ │Link Control │ │Statistics   │ │Stream Intgr │   │ │
│  └─────────────┘ └─────────────┘ └─────────────┘ └─────────────┘   │ │
└─────────────────────────────────────────────────────────────────────┘ │
                                  │                                     │
                                  ▼                                     │
                        ┌─────────────────┐                             │
                        │   CALLBACKS     │◄────────────────────────────┘
                        │ TO CHAIN NET    │
                        └─────────────────┘
```

## 🔍 API Functions Analysis

### 1. Initialization & Configuration

| Function | Called From | Purpose | Threading Impact |
|----------|-------------|---------|------------------|
| `dap_link_manager_init()` | `dap_chain_net.c:253` | Инициализация с колбеками Chain Net | **HIGH** - создает timer thread |
| `dap_link_manager_deinit()` | `dap_chain_net.c:1787` | Деинициализация и cleanup | **HIGH** - requires careful shutdown |
| `dap_link_manager_new()` | Internal use | Создание экземпляра | **MEDIUM** - rwlock init |
| `dap_link_manager_get_default()` | `dap_global_db_cluster.c:145` | Получение singleton | **LOW** - read-only |

**Refactoring Priority**: 🔥 **CRITICAL** - первый компонент для actor pattern

### 2. Network Management

| Function | Called From | Purpose | Threading Impact |
|----------|-------------|---------|------------------|
| `dap_link_manager_add_net()` | `dap_chain_net.c:2217` | Добавление сети в управление | **HIGH** - nets_lock write |
| `dap_link_manager_add_net_associate()` | Multiple consensus modules | Ассоциация кластера с сетью | **HIGH** - nets_lock write |
| `dap_link_manager_remove_net()` | Internal cleanup | Удаление сети | **HIGH** - complex cleanup |
| `dap_link_manager_set_net_condition()` | `dap_chain_net.c:3347,3350` | Активация/деактивация сети | **HIGH** - state changes |
| `dap_link_manager_get_net_condition()` | `dap_chain_ch.c:794,1019,1264` | Проверка состояния сети | **MEDIUM** - read operation |

**Refactoring Priority**: 🔥 **HIGH** - сложная синхронизация с nets_lock

### 3. Statistics & Information

| Function | Called From | Purpose | Threading Impact |
|----------|-------------|---------|------------------|
| `dap_link_manager_links_count()` | `dap_chain_net.c:632,891,921` + internal | Подсчет активных соединений | **MEDIUM** - read with cluster access |
| `dap_link_manager_required_links_count()` | `dap_chain_net.c:892` | Минимальное количество соединений | **LOW** - config read |
| `dap_link_manager_needed_links_count()` | `dap_chain_net_balancer.c:477` | Недостающие соединения | **MEDIUM** - calculation |
| `dap_link_manager_get_net_links_addrs()` | `dap_chain_net_balancer.c:84` <br> `dap_chain_node.c:92` | Список адресов соединений | **HIGH** - complex read with filtering |
| `dap_link_manager_get_ignored_addrs()` | `dap_chain_net_balancer.c:85` | Hot list адреса | **MEDIUM** - Global DB access |

**Refactoring Priority**: 🟡 **MEDIUM** - read operations, но сложные

### 4. Link Operations

| Function | Called From | Purpose | Threading Impact |
|----------|-------------|---------|------------------|
| `dap_link_manager_link_create()` | `dap_chain_net.c:392` | Создание нового соединения | **HIGH** - links_lock write + complex logic |
| `dap_link_manager_link_update()` | `dap_chain_net.c:396,567` | Обновление параметров соединения | **HIGH** - async callback with state change |
| `dap_link_manager_link_find()` | `dap_chain_net.c:387` | Поиск существующего соединения | **MEDIUM** - links_lock read |

**Refactoring Priority**: 🔥 **HIGH** - критические операции с links_lock

### 5. Stream Integration

| Function | Called From | Purpose | Threading Impact |
|----------|-------------|---------|------------------|
| `dap_link_manager_stream_add()` | `dap_stream.c:953,954` | Уведомление о новом потоке | **CRITICAL** - async callback from stream thread |
| `dap_link_manager_stream_replace()` | `dap_stream.c:977,978` | Замена направления потока | **CRITICAL** - async callback with state change |
| `dap_link_manager_stream_delete()` | `dap_stream.c:980,981` | Удаление потока | **CRITICAL** - async callback with cleanup |

**Refactoring Priority**: 🔥 **CRITICAL** - эти функции вызываются из других потоков!

### 6. Cluster Management (Callbacks)

| Function | Called From | Purpose | Threading Impact |
|----------|-------------|---------|------------------|
| `dap_link_manager_add_links_cluster()` | `dap_global_db_cluster.c:130` (callback) | Добавление в active cluster | **HIGH** - cluster callback thread |
| `dap_link_manager_remove_links_cluster()` | `dap_global_db_cluster.c:131` (callback) | Удаление из active cluster | **HIGH** - cluster callback thread |
| `dap_link_manager_add_static_links_cluster()` | `dap_global_db_cluster.c:163` (callback) | Добавление в static cluster | **HIGH** - cluster callback thread |
| `dap_link_manager_remove_static_links_cluster()` | `dap_global_db_cluster.c:164` (callback) | Удаление из static cluster | **HIGH** - cluster callback thread |

**Refactoring Priority**: 🔥 **CRITICAL** - callbacks из cluster threads

### 7. Network Accounting

| Function | Called From | Purpose | Threading Impact |
|----------|-------------|---------|------------------|
| `dap_link_manager_accounting_link_in_net()` | `dap_stream_ch_chain_net.c:144,176` | Учет соединения в сети | **HIGH** - async callback with complex logic |

**Refactoring Priority**: 🔥 **HIGH** - сложная асинхронная логика

### 8. Control Functions

| Function | Called From | Purpose | Threading Impact |
|----------|-------------|---------|------------------|
| `dap_link_manager_set_condition()` | Internal init/deinit | Активация/деактивация менеджера | **MEDIUM** - simple state change |
| `dap_link_manager_get_condition()` | Internal checks | Проверка активности | **LOW** - atomic read |

**Refactoring Priority**: 🟢 **LOW** - простые операции

## 🔄 Callback Functions Analysis

### Callbacks FROM Link Manager TO Chain Net

| Callback | Implementation | Purpose | Threading Context |
|----------|----------------|---------|-------------------|
| `fill_net_info` | `s_link_manager_fill_net_info()` | Заполнение адреса/порта для соединения | Query thread (timer) |
| `link_request` | `s_link_manager_link_request()` | Запрос новых соединений | Query thread (timer) |
| `connected` | `s_link_manager_callback_connected()` | Успешное установление соединения | Client thread (async) |
| `error` | `s_link_manager_callback_error()` | Ошибка соединения | Client thread (async) |
| `disconnected` | `s_link_manager_callback_disconnected()` | Разрыв соединения | Client thread (async) |
| `link_count_changed` | `s_link_manager_link_count_changed()` | Изменение количества соединений | Various threads |

**Refactoring Priority**: 🔥 **CRITICAL** - mixed threading contexts!

## ⚡ Threading Hotspots

### 🔥 Critical Race Conditions

1. **Stream Integration Functions**
   ```c
   // Вызываются из stream thread
   dap_link_manager_stream_add(&a_stream->node, a_stream->is_client_to_uplink);
   dap_link_manager_stream_replace(&a_stream->node, l_stream->is_client_to_uplink);
   dap_link_manager_stream_delete(&a_stream->node);
   ```
   **Problem**: Stream thread → Link Manager (query thread) → rwlock contention

2. **Cluster Callbacks**
   ```c
   // Регистрируются как cluster callbacks
   l_cluster->links_cluster->members_add_callback = dap_link_manager_add_links_cluster;
   l_cluster->links_cluster->members_delete_callback = dap_link_manager_remove_links_cluster;
   ```
   **Problem**: Cluster thread → Link Manager locks → potential deadlock

3. **Connection State Changes**
   ```c
   // Client callbacks из разных потоков
   s_client_connected_callback() → Link Manager state change
   s_client_error_callback() → Link Manager cleanup
   ```
   **Problem**: Client threads → synchronized state changes

### 🟡 Performance Bottlenecks

1. **Statistics Queries**
   - `dap_link_manager_links_count()` - вызывается часто для UI/JSON
   - `dap_link_manager_get_net_links_addrs()` - complex filtering with locks

2. **Hot List Operations**
   - Global DB sync operations в `s_update_hot_list()`
   - Periodic cleanup с blocking operations

## 🎯 Actor Pattern Migration Strategy

### Phase 1: Message Types Mapping

| Current Function | Actor Message Type | Priority | Complexity |
|------------------|-------------------|----------|------------|
| `stream_add/replace/delete` | `LM_MSG_STREAM_*` | CRITICAL | Medium |
| `link_create/update` | `LM_MSG_LINK_*` | HIGH | High |
| `add/remove_net` | `LM_MSG_NET_*` | HIGH | Medium |
| `accounting_link_in_net` | `LM_MSG_ACCOUNTING` | HIGH | High |
| `links_count` | `LM_MSG_GET_STATS` | MEDIUM | Low |
| `get_net_links_addrs` | `LM_MSG_GET_ADDRS` | MEDIUM | Medium |

### Phase 2: Threading Categories

#### 🔥 Immediate Actor Messages (High Priority Queue)
- Stream integration calls (from stream threads)
- Connection state changes (from client threads)
- Cluster membership changes (from cluster threads)

#### 🟡 Regular Actor Messages (Normal Priority Queue)
- Network management operations
- Link creation/updates
- Statistics updates

#### 🟢 Background Actor Messages (Low Priority Queue)
- Hot list cleanup
- Periodic maintenance
- Debug/logging operations

### Phase 3: Callback Refactoring

#### Current Problem:
```c
// Mixed thread contexts
timer_thread → fill_net_info() → Chain Net
client_thread → connected() → Chain Net  
cluster_thread → add_links_cluster() → Link Manager
```

#### Actor Solution:
```c
// All callbacks через actor thread
actor_thread → callback_queue → Chain Net
// Результаты через async message passing
Chain Net → result_callback → original_thread
```

## 📋 Refactoring Checklist

### ✅ Immediate Actions (1-2 дня)
- [ ] Создать message types enum
- [ ] Реализовать actor infrastructure
- [ ] Migrate stream integration functions (highest risk)

### 🔶 Phase 1 Migration (1 неделя)
- [ ] Convert link operations to messages
- [ ] Convert network management to messages
- [ ] Implement sync→async wrappers

### 🔷 Phase 2 Migration (1-2 недели)
- [ ] Migrate statistics functions
- [ ] Convert cluster callbacks
- [ ] Implement callback queuing

### 🔷 Phase 3 Optimization (2-3 недели)
- [ ] Remove all rwlocks
- [ ] Implement batch processing
- [ ] Add performance monitoring

## 🚨 High-Risk Areas

1. **Stream Integration** - вызывается из stream threads, высокий риск race conditions
2. **Client Callbacks** - асинхронные callbacks из client threads
3. **Cluster Callbacks** - potential circular dependencies
4. **Hot List** - Global DB operations могут блокировать

## 📊 Success Metrics

- ❌ **Zero rwlock operations** в Link Manager
- ⚡ **<1ms latency** для большинства operations
- 🔄 **2-3x throughput** improvement
- 🚫 **Zero deadlocks** в production
- 📈 **95%+ success rate** для connection operations

---

**Готово для реализации Actor Pattern**: Все critical paths identified and categorized! 