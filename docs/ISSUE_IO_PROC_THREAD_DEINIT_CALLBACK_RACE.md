# ISSUE: race/hang при concurrent `dap_proc_thread_callback_add*()` и `dap_proc_thread_deinit()`

## Кратко
Есть окно гонки между проверкой валидности `dap_proc_thread_t*` и использованием `queue_lock`.  
При конкурентном `dap_proc_thread_deinit()` это приводит к undefined behavior и воспроизводимому зависанию.

## Затронутый код
- `module/io/dap_proc_thread.c:217` (`s_proc_thread_handle_is_valid()` проверка)
- `module/io/dap_proc_thread.c:230` (`pthread_mutex_lock(&l_thread->queue_lock)`)
- `module/io/dap_proc_thread.c:323` (`pthread_cond_destroy(&l_thread->queue_event)`)
- `module/io/dap_proc_thread.c:325` (`pthread_mutex_destroy(&l_thread->queue_lock)`)
- `module/io/dap_proc_thread.c:331` (`l_thread->context = NULL` выставляется слишком поздно)

## Сценарий проблемы
1. Поток A вызывает `dap_proc_thread_callback_add_pri()`.
2. Он проходит `s_proc_thread_handle_is_valid()` (контекст еще не `NULL`).
3. Параллельно поток B завершает proc-thread и уничтожает `queue_lock/queue_event`.
4. Поток A доходит до `pthread_mutex_lock()` уже по уничтоженному mutex.

## Воспроизведение
Локальный stress-harness с producer-потоком, который непрерывно вызывает `dap_proc_thread_callback_add()`, и параллельным `dap_proc_thread_deinit()`.  
Повторяемо ловится `timeout` (в моем прогоне падало, например, на 20-й/30-й итерации).

## Ожидаемое поведение
- API либо корректно сериализует deinit/add, либо стабильно возвращает ошибку (`-4`, stale handle), без зависаний.

## Фактическое поведение
- Зависание на `pthread_join` producer-потока (косвенный симптом deadlock/UB в области `queue_lock` lifecycle).

## Риск
- High: потенциальный deadlock в production при shutdown/restart и фоновых отправках callback.

## Предложение по исправлению
- Сделать lifecycle `queue_lock/queue_event/context` атомарно согласованным:
  - до destroy lock/event пометить thread state как `stopping/dead` (atomic flag), чтобы add-path сразу отвергал запрос;
  - либо брать дополнительную глобальную синхронизацию на add/deinit;
  - `context = NULL` выставлять до destroy lock/event при корректной memory-order синхронизации и без окна TOCTOU.
- Добавить регрессионный тест именно на concurrent `callback_add` vs `deinit`.

