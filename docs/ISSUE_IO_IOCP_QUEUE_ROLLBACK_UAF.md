# ISSUE: возможный UAF в IOCP очереди при rollback после `PostQueuedCompletionStatus` failure

## Кратко
В IOCP ветке при ошибке `PostQueuedCompletionStatus()` выполняется rollback через `InterlockedPopEntrySList()`.  
Если popped-элемент не тот, который только что добавили, в очереди может остаться "наш" указатель, а вызывающий код уже освобождает сообщение как failed-send. Это создает риск use-after-free.

## Затронутый код
- `module/io/dap_events_socket.c:262` (ошибка `PostQueuedCompletionStatus`)
- `module/io/dap_events_socket.c:263` (rollback pop)
- `module/io/dap_events_socket.c:267` (push обратно "чужого" элемента)
- `module/io/dap_events_socket.c:270` (возврат ошибки)

Код, который освобождает payload при `queue_ptr_send != 0`:
- `module/io/dap_events_socket.c:498`
- `module/io/dap_events_socket.c:1633`
- `module/io/dap_events_socket.c:1676`
- `module/io/dap_events_socket.c:1721`

## Сценарий проблемы
1. Указатель на сообщение помещен в lock-free очередь.
2. Сигнал в IOCP не отправился (`PostQueuedCompletionStatus` failed).
3. Rollback не гарантирует, что удаляется именно этот новый элемент.
4. Функция возвращает ошибку, caller освобождает сообщение.
5. Очередь позже может отдать dangling pointer в `queue_ptr_callback`.

## Ожидаемое поведение
- На failed-send нельзя оставлять в очереди элемент, владение которым caller считает переданным/откатанным.

## Фактическое поведение
- Возможна несогласованность владения объектом между очередью и caller.

## Риск
- High для Windows/IOCP: потенциальный use-after-free, случайные падения и memory corruption.

## Предложение по исправлению
- Гарантировать корректный ownership contract:
  - либо "после успешного push ownership безусловно у очереди", и caller никогда не освобождает payload на failed IOCP signal;
  - либо обеспечивать точный rollback именно добавленного элемента (например, tagged node/перепроектирование очереди).
- Добавить IOCP-specific test, который эмулирует fail `PostQueuedCompletionStatus` под конкурентной нагрузкой.

