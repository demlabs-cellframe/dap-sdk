# Статус тестов DAP SDK - Краткое резюме

**Дата**: 2026-02-02 (обновлено)  
**Результат**: 35/39 тестов проходят (90%)

---

## ✅ Исправленные тесты

### test_thread_pool (#16) ✅ ИСПРАВЛЕН
- **Проблема**: Баг в тестовом коде - использование compound literal `&(int){i+1}`
- **Решение**: Заменили на static массивы в функциях:
  - `test_thread_pool_shutdown()`
  - `test_thread_pool_pending_count()`
  - `test_thread_pool_queue_overflow()`
  - `test_thread_pool_stress()`

---

## ❌ Падающие тесты (требуют исправления)

### 1-2. test_http_simple (#35), test_stream (#36) 🔴 RACE CONDITION
- **Проблема**: Segfault при cleanup, НО под valgrind не падают
- **Вывод**: Это **race condition**, а не простой use-after-free
- **Утечки памяти**: dap_config, dap_http_server, dap_context_queue
- **Требует**: ThreadSanitizer или добавление барьеров синхронизации
- **Файл**: `core/dap_events_socket.c`, `io/dap_context.c`

### 3. test_trans_api (#37) 🔴 CRITICAL  
- **Проблема**: Segfault (требуется диагностика)
- **Требует**: Запуск с gdb для stack trace

### 4. test_trans_integration (#38) 🟡 MEDIUM
- **Проблема**: eBPF requires CAP_BPF capability
- **Исправление**: Добавить fallback если нет прав (1-2 часа)

---

## ✅ Работающие модули

- ✅ Framework тесты (6/6) - 100%
- ✅ Core (6/6) - 100%  
- ✅ **Crypto (3/3) - 100%** 🎉 Квантово-устойчивая крипта работает!
- ✅ Network базовый (10/10) - 100%
- ✅ IO/Threading (4/4) - 100% 🎉 **ИСПРАВЛЕНО!**
- ✅ Global-DB (1/1) - 100%

---

## 🔧 Быстрые команды

### Отладить race condition
```bash
cd cellframe-node/dap-sdk/build
# ThreadSanitizer (пересобрать с -fsanitize=thread)
cmake .. -DCMAKE_C_FLAGS="-fsanitize=thread -g" -DCMAKE_CXX_FLAGS="-fsanitize=thread -g"
make test_http_simple && ./tests/integration/net/http/test_http_simple

# Valgrind показывает утечки памяти (тест не падает!)
valgrind --leak-check=full ./tests/integration/net/http/test_http_simple
```

### Перезапустить только упавшие тесты
```bash
cd cellframe-node/dap-sdk/build
ctest --rerun-failed --output-on-failure
```

---

## 📊 Приоритеты

1. ✅ ~~**test_thread_pool**~~ - **ИСПРАВЛЕН**
2. **race condition cleanup** (4-8 часов) - ThreadSanitizer, барьеры синхронизации
3. **eBPF fallback** (1-2 часа) - улучшение совместимости

**Текущий статус**: 35/39 тестов (90%) ✅

---

Подробный анализ: `.context/test_failures_analysis.md`  
Полные результаты: `.context/test_results.json`
