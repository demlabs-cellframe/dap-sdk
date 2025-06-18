# Minimal Race Conditions Fix Plan

## 🎯 Цель: Устранить критические race conditions минимальными изменениями

**Анализ текущего состояния**: Архитектура уже почти готова! `s_query_thread` используется правильно, но ещё остались блокировки внутри callbacks.

## ✅ Что уже хорошо работает

### 1. Асинхронная архитектура уже есть:
```c
// s_query_thread инициализируется один раз при инициализации (строка 207)
if (!(s_query_thread = dap_proc_thread_get_auto())) {
    log_it(L_ERROR, "Can't choose query thread on link manager");
    return -1;
}

// Stream Integration Functions уже асинхронные:
int dap_link_manager_stream_add(dap_stream_node_addr_t *a_node_addr, bool a_uplink) {
    // ...
    return dap_proc_thread_callback_add_pri(s_query_thread, s_stream_add_callback, l_args, DAP_QUEUE_MSG_PRIORITY_HIGH);
}

// Client callbacks уже асинхронные:
void s_client_error_callback(dap_client_t *a_client, void *a_arg) {
    // ...
    dap_proc_thread_callback_add_pri(s_query_thread, s_link_drop_callback, l_args, DAP_QUEUE_MSG_PRIORITY_HIGH);
}
```

### 2. Приоритеты уже есть:
- `DAP_QUEUE_MSG_PRIORITY_HIGH` для критичных operations
- `DAP_QUEUE_MSG_PRIORITY_NORMAL` для обычных operations

## 🔥 Критические проблемы (требуют немедленного исправления)

### Problem #1: Блокировки внутри s_query_thread callbacks

**Местоположение**: Все callback функции в `s_query_thread` используют rwlock

```c
// ❌ ПРОБЛЕМА: rwlock внутри s_query_thread (строки 970, 1034, 1071, 1118)
static bool s_stream_add_callback(void *a_arg) {
    pthread_rwlock_wrlock(&s_link_manager->links_lock);  // ❌ Блокировка!
    // ... логика ...
    pthread_rwlock_unlock(&s_link_manager->links_lock);
}
```

**Решение**: Поскольку все операции выполняются в single `s_query_thread`, блокировки не нужны!

### Problem #2: s_links_wake_up() использует rwlock в timer callback

**Местоположение**: `s_links_wake_up()` строка 669

```c
// ❌ ПРОБЛЕМА: rwlock в timer thread (может конфликтовать с async callbacks)
void s_links_wake_up(dap_link_manager_t *a_link_manager) {
    pthread_rwlock_wrlock(&a_link_manager->links_lock);  // ❌ Блокировка!
    // ... логика ...
    pthread_rwlock_unlock(&a_link_manager->links_lock);
}
```

### Problem #3: Cluster callbacks ещё не асинхронные

**Местоположение**: `dap_link_manager_add_links_cluster()` строка 449

```c
// ❌ ПРОБЛЕМА: прямые вызовы из cluster threads
void dap_link_manager_add_links_cluster(dap_cluster_member_t *a_member, void UNUSED_ARG *a_arg) {
    // Вызывается напрямую из cluster thread, нет async callback
    dap_link_t *l_link = s_link_manager_link_find(&a_member->addr);  // Прямое использование!
}
```

## 🛠️ Минималистичный план исправлений

### Фаза 1: Убрать блокировки из s_query_thread callbacks (1 день)

Поскольку все callbacks выполняются в одном `s_query_thread`, блокировки не нужны:

```c
// ✅ ИСПРАВЛЕНИЕ: Убрать все rwlock из callback функций
static bool s_stream_add_callback(void *a_arg) {
    assert(a_arg);
    struct link_moving_args *l_args = a_arg;
    dap_stream_node_addr_t *l_node_addr = &l_args->addr;
    
    // ❌ УБРАТЬ: pthread_rwlock_wrlock(&s_link_manager->links_lock);
    
    dap_link_t *l_link = s_link_manager_link_find(l_node_addr);
    if (!l_link && !l_args->uplink)
        l_link = s_link_manager_downlink_create(l_node_addr);
    // ... остальная логика без изменений ...
    
    // ❌ УБРАТЬ: pthread_rwlock_unlock(&s_link_manager->links_lock);
    
    DAP_DELETE(l_args);
    return false;
}
```

**Изменяемые функции:**
- `s_stream_add_callback()` - убрать rwlock
- `s_stream_replace_callback()` - убрать rwlock  
- `s_stream_delete_callback()` - убрать rwlock
- `s_link_accounting_callback()` - убрать rwlock
- `s_link_drop_callback()` - убрать rwlock

### Фаза 2: Сделать s_links_wake_up() асинхронной (1 день)

```c
// ✅ ИСПРАВЛЕНИЕ: Перенести s_links_wake_up в s_query_thread
void s_update_states(void *a_arg) {
    // ... sanity checks ...
    
    static bool l_wakeup_mode = false;
    if (l_wakeup_mode) {
        // ❌ УБРАТЬ: s_links_wake_up(l_link_manager);
        // ✅ ДОБАВИТЬ: async callback
        dap_proc_thread_callback_add(s_query_thread, s_links_wake_up_callback, l_link_manager);
    } else {
        s_links_request(l_link_manager);  // Эта функция уже безопасна
    }
    l_wakeup_mode = !l_wakeup_mode;
}

// ✅ НОВАЯ ФУНКЦИЯ: async wrapper
static bool s_links_wake_up_callback(void *a_arg) {
    dap_link_manager_t *a_link_manager = a_arg;
    
    // Теперь выполняется в s_query_thread, rwlock не нужен
    // ❌ УБРАТЬ: pthread_rwlock_wrlock(&a_link_manager->links_lock);
    
    dap_time_t l_now = dap_time_now();
    dap_link_t *it, *tmp;
    HASH_ITER(hh, a_link_manager->links, it, tmp) {
        // ... вся логика без изменений, но без rwlock ...
    }
    
    // ❌ УБРАТЬ: pthread_rwlock_unlock(&a_link_manager->links_lock);
    return false;  // one-shot callback
}
```

### Фаза 3: Сделать cluster callbacks асинхронными (1 день)

```c
// ✅ ИСПРАВЛЕНИЕ: async cluster callbacks
void dap_link_manager_add_links_cluster(dap_cluster_member_t *a_member, void UNUSED_ARG *a_arg) {
    dap_return_if_pass(!s_link_manager || !a_member || !a_member->cluster);
    
    // Создать payload для async callback
    typedef struct {
        dap_stream_node_addr_t addr;
        dap_cluster_t *cluster;
        bool adding;  // true = add, false = remove
    } cluster_operation_args_t;
    
    cluster_operation_args_t *l_args = DAP_NEW_Z_RET_IF_FAIL(cluster_operation_args_t);
    l_args->addr = a_member->addr;
    l_args->cluster = a_member->cluster;
    l_args->adding = true;
    
    // ✅ ДОБАВИТЬ: отправить в s_query_thread
    dap_proc_thread_callback_add_pri(s_query_thread, s_cluster_operation_callback, l_args, DAP_QUEUE_MSG_PRIORITY_HIGH);
}

// ✅ НОВАЯ ФУНКЦИЯ: unified cluster callback
static bool s_cluster_operation_callback(void *a_arg) {
    cluster_operation_args_t *l_args = a_arg;
    
    // Выполняется в s_query_thread, rwlock не нужен
    dap_link_t *l_link = s_link_manager_link_find(&l_args->addr);
    if (!l_link) {
        log_it(L_ERROR, "Try cluster operation on non-existent link");
        DAP_DELETE(l_args);
        return false;
    }
    
    if (l_args->adding) {
        l_link->active_clusters = dap_list_append(l_link->active_clusters, l_args->cluster);
    } else {
        l_link->active_clusters = dap_list_remove(l_link->active_clusters, l_args->cluster);
    }
    
    s_debug_cluster_adding_removing(false, l_args->adding, l_args->cluster, &l_args->addr);
    DAP_DELETE(l_args);
    return false;
}
```

## 📊 Impact Assessment

### ✅ Benefits:
- **100% elimination** всех rwlock operations в Link Manager
- **Zero deadlock possibility** - single thread execution
- **Предсказуемая latency** - no lock contention
- **Minimal code changes** - infrastructure уже есть

### ⚠️ Risks:
- **Low risk** - изменения очень локальные
- **Regression risk** - minimal, основная логика не меняется
- **Performance risk** - negligible, убираем overhead блокировок

### 🧪 Testing Strategy:
1. **Unit tests**: каждый callback отдельно
2. **Integration tests**: полный lifecycle соединений
3. **Stress tests**: высокая нагрузка без race conditions
4. **Thread safety**: подтвердить отсутствие deadlocks

## 📋 Implementation Checklist

### Day 1: Remove rwlocks from callbacks
- [ ] Remove rwlock from `s_stream_add_callback()`
- [ ] Remove rwlock from `s_stream_replace_callback()`
- [ ] Remove rwlock from `s_stream_delete_callback()`
- [ ] Remove rwlock from `s_link_accounting_callback()`
- [ ] Remove rwlock from `s_link_drop_callback()`
- [ ] Test: basic functionality works

### Day 2: Async s_links_wake_up
- [ ] Create `s_links_wake_up_callback()` 
- [ ] Modify `s_update_states()` to use async callback
- [ ] Remove rwlock from wake up logic
- [ ] Test: timer operations work correctly

### Day 3: Async cluster callbacks  
- [ ] Create unified `s_cluster_operation_callback()`
- [ ] Modify `dap_link_manager_add_links_cluster()`
- [ ] Modify `dap_link_manager_remove_links_cluster()`
- [ ] Modify static cluster functions similarly
- [ ] Test: cluster operations work correctly

### Day 4: Final testing & cleanup
- [ ] Integration testing
- [ ] Stress testing  
- [ ] Remove unused rwlock variables
- [ ] Update documentation

## 🎯 Expected Results

**После исправлений:**
- ✅ Zero rwlock operations in Link Manager
- ✅ Zero deadlock possibilities  
- ✅ All operations in single `s_query_thread`
- ✅ Preserved existing API compatibility
- ✅ Improved performance (no lock contention)

**Success Metrics:**
- 🚫 Zero `pthread_rwlock_*` calls in Link Manager
- ⚡ <1ms latency for most operations  
- 📈 No regression in functionality
- 🔒 No deadlocks in stress testing

---

**Status**: ✅ Ready for implementation - minimal, low-risk approach
**Timeline**: 3-4 дня with thorough testing
**Risk Level**: **LOW** - leverages existing infrastructure 