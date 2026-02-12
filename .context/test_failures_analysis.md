# Детальный анализ падающих тестов DAP SDK

Дата: 2026-02-02  
Всего тестов: 39  
Прошло: 34 (87.2%)  
Упало: 5 (12.8%)

---

## 1. test_thread_pool (Тест #16)

### Статус
❌ **CRITICAL** - Subprocess aborted (SIGABRT, exit code 134)

### Проблема
Тест `test_thread_pool_shutdown()` падает на проверке:
```c
dap_assert(atomic_load(&s_task_counter) == 15, "All tasks should complete before shutdown");
```

### Исходная причина
**BUG В ТЕСТОВОМ КОДЕ** (строка 209):
```c
for (int i = 0; i < 5; i++) {
    dap_thread_pool_submit(l_pool, s_simple_task, &(int){i + 1}, NULL, NULL);
}
```

Проблема: `&(int){i + 1}` создает compound literal, который является временным объектом. Его время жизни заканчивается после выполнения выражения, но задачи в thread pool выполняются асинхронно и пытаются обратиться к уже несуществующему объекту.

### Решение
Заменить на массив:
```c
int l_args[5];
for (int i = 0; i < 5; i++) {
    l_args[i] = i + 1;
    dap_thread_pool_submit(l_pool, s_simple_task, &l_args[i], NULL, NULL);
}
```

### Местоположение
- **Файл**: `tests/unit/io/test_thread_pool.c`
- **Функция**: `test_thread_pool_shutdown()`
- **Строка**: 209

### Приоритет
🔴 **HIGH** - Баг в тестовом коде, не в самой реализации thread pool

---

## 2. test_http_simple (Тест #35)

### Статус
❌ **CRITICAL** - Segmentation Fault (SIGSEGV, exit code 139)

### Проблема
Segfault при очистке ресурсов событийной системы во время shutdown HTTP сервера.

### Точка падения
```
[DBG] [dap_events_socket] Remove es 0x7f8a7c000ba0 [6] "EVENT" uuid 0x00000000
[DBG] [dap_events_socket] Deleting esocket 0x00000000 type EVENT
[DBG] [dap_events_socket] Release es 0x7f8a7c000ba0 "EVENT" uuid 0x00000000
*** Segmentation Fault ***
```

### Исходная причина
**Use-after-free** или **double-free** в функции `dap_events_socket_release()` при очистке EVENT socket'ов.

Возможные причины:
1. Сокет уже был освобожден ранее
2. Некорректный reference counting
3. Попытка доступа к освобожденной памяти в контексте worker'ов

### Рекомендации по исследованию
```bash
# Запустить с valgrind для детальной диагностики
valgrind --leak-check=full --track-origins=yes \
         ./tests/integration/net/http/test_http_simple

# Запустить с AddressSanitizer
ASAN_OPTIONS=detect_leaks=1 ./tests/integration/net/http/test_http_simple

# Отладка с gdb
gdb --args ./tests/integration/net/http/test_http_simple
```

### Местоположение
- **Файл**: `core/dap_events_socket.c` (предположительно функция `dap_events_socket_release`)
- **Тест**: `tests/integration/net/http/test_http_simple.c`

### Приоритет
🔴 **CRITICAL** - Приводит к crash при завершении HTTP сервера

---

## 3. test_stream (Тест #36)

### Статус
❌ **CRITICAL** - Segmentation Fault (SIGSEGV, exit code 139)

### Проблема
Аналогична test_http_simple - segfault при очистке событийных сокетов.

### Точка падения
```
[DBG] [dap_events_socket] Remove es 0x7fd770000ba0 [7] "EVENT" uuid 0x00000000
[DBG] [dap_events_socket] Deleting esocket 0x00000000 type EVENT
[DBG] [dap_events_socket] Release es 0x7fd770000ba0 "EVENT" uuid 0x00000000
*** Segmentation Fault ***
```

### Исходная причина
**Та же проблема** что и в test_http_simple - некорректная очистка EVENT socket'ов в `dap_events_socket_release()`.

### Паттерн
Обе проблемы (test_http_simple и test_stream) имеют идентичный stack trace:
- Происходит при shutdown контекста (Context #0 finished)
- Падает при удалении EVENT socket'а с uuid 0x00000000
- fd = 7 в обоих случаях - это event_fd для context queue

### Гипотеза
Проблема в порядке очистки ресурсов при завершении контекста:
1. Context queue удаляется
2. Event fd закрывается
3. Попытка освободить esocket, который уже был частично очищен

### Рекомендации
Проверить последовательность в:
- `dap_worker_thread_deinit()` или аналогичной функции cleanup
- `dap_context_deinit()`
- Убедиться что event sockets освобождаются до закрытия их fd

### Приоритет
🔴 **CRITICAL** - Системная проблема, влияет на несколько компонентов

---

## 4. test_trans_api (Тест #37)

### Статус
❌ **CRITICAL** - Segmentation Fault (SIGSEGV, exit code 139)

### Проблема
Segfault при выполнении теста transport API.

### Детали
Информации недостаточно для точной диагностики. Требуется запуск с отладчиком.

### Рекомендации
```bash
# Детальная диагностика
valgrind --leak-check=full --track-origins=yes \
         ./tests/integration/net/trans/test_trans_api

# С gdb для получения stack trace
gdb --args ./tests/integration/net/trans/test_trans_api
(gdb) run
(gdb) bt full  # при падении
```

### Приоритет
🔴 **CRITICAL** - Нужна дополнительная диагностика

---

## 5. test_trans_integration (Тест #38)

### Статус
❌ **HIGH** - Subprocess aborted

### Проблема
Тест падает с исключением, дополнительно появляется ошибка:
```
[ERR] [dap_io_flow_ebpf] Failed to load eBPF program: Operation not permitted (errno=1)
```

### Исходная причина
**Недостаточные привилегии** для загрузки eBPF программы.

eBPF требует одно из:
- `CAP_BPF` capability (Linux 5.8+)
- `CAP_SYS_ADMIN` capability (старые версии)
- root привилегии

### Решение
Два варианта:

1. **Короткий срок** - пропускать eBPF функционал если нет прав:
```c
if (!dap_io_flow_ebpf_available()) {
    log_it(L_WARNING, "eBPF not available, falling back to userspace");
    // fallback implementation
}
```

2. **Долгий срок** - документировать требования:
- Добавить проверку capabilities перед тестом
- Пропускать тест с предупреждением если нет прав
- Добавить в README требования для запуска полного test suite

### Рекомендации
```bash
# Проверить доступность eBPF
capsh --print | grep cap_bpf

# Запустить с нужными capabilities (только для тестирования!)
sudo setcap cap_bpf=ep ./tests/integration/net/trans/test_trans_integration

# Или запустить как root (не рекомендуется для production)
sudo ./tests/integration/net/trans/test_trans_integration
```

### Приоритет
🟡 **MEDIUM** - Не critical, но нужно graceful fallback

---

## Общие рекомендации

### 1. Немедленные действия

1. **Исправить test_thread_pool** (1-2 часа):
   - Заменить compound literals на массив
   - Убедиться что все похожие тесты не имеют такой проблемы

2. **Диагностировать event socket cleanup** (2-4 часа):
   - Запустить падающие тесты с valgrind
   - Получить точный stack trace с gdb
   - Найти место use-after-free

3. **Добавить fallback для eBPF** (1-2 часа):
   - Проверять доступность eBPF перед использованием
   - Graceful degradation если нет прав

### 2. Инструменты отладки

```bash
# Valgrind для memory issues
valgrind --leak-check=full --track-origins=yes --show-leak-kinds=all \
         --verbose ./test_binary

# AddressSanitizer (пересобрать с флагами)
cmake -DCMAKE_C_FLAGS="-fsanitize=address -g" ..
make

# Thread Sanitizer для race conditions
cmake -DCMAKE_C_FLAGS="-fsanitize=thread -g" ..
make

# GDB для stack traces
gdb --args ./test_binary
(gdb) run
(gdb) bt full
(gdb) frame N
(gdb) print variable_name
```

### 3. Долгосрочные улучшения

1. **CI/CD интеграция**:
   - Запускать тесты с valgrind в CI
   - Блокировать merge при memory leaks
   - Автоматические отчеты о падающих тестах

2. **Улучшение тестов**:
   - Добавить timeout для всех тестов
   - Better error messages при падении
   - Автоматический cleanup при assert failure

3. **Документация**:
   - Требования для запуска тестов
   - Troubleshooting guide
   - Примеры отладки с valgrind/gdb

---

## Статус модулей

### ✅ Полностью работающие (100% тестов проходят)

- **Framework** (6/6): Тестовый фреймворк работает идеально
- **Core** (6/6): Базовая функциональность стабильна
- **Crypto** (3/3): 🎉 Квантово-устойчивая криптография работает!
- **Network (базовый)** (10/10): HTTP, транспорты, stream packet - OK

### ⚠️ Частично работающие

- **IO/Threading** (3/4): 
  - ✅ test_thread (OK)
  - ✅ test_io_flow_tiers (OK)
  - ✅ test_thread_pool_integration (OK)
  - ❌ test_thread_pool (unit) - баг в тесте

### ❌ Проблемные

- **Network (integration)** (2/5):
  - ❌ test_http_simple - event socket cleanup
  - ❌ test_stream - event socket cleanup
  - ❌ test_trans_api - неизвестно
  - ❌ test_trans_integration - eBPF permissions
  - ✅ test_http_client_server (OK)

---

## Выводы

1. **test_thread_pool** - простая ошибка в тестовом коде, легко исправить
2. **event socket cleanup** - системная проблема, требует внимательной отладки
3. **eBPF permissions** - архитектурная проблема, нужен fallback
4. **Общее качество**: 87% тестов проходят, большинство core модулей стабильны
5. **Криптография**: Полностью работоспособна, включая квантово-устойчивые алгоритмы

**Оценка времени на исправление**: 1-2 дня для всех критических проблем
