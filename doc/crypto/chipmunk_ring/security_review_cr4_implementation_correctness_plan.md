# CR-4 Security Review (Chipmunk Ring): корректность реализации

## Цель CR-4

Проверить консистентность инвариантов на уровне реализации после внедрения CR-1..CR-3:
- корректность API обработки ошибок;
- воспроизводимость сериализации `chipmunk_ring_signature_*`;
- устойчивость к небезопасным `NULL`/`0` входам и частичным данным;
- корректное поведение `sign/verify` после сериализационного контура.

## 1) Покрытие файлов

- `module/crypto/src/sig/chipmunk/chipmunk_ring.c`
  - `chipmunk_ring_signature_to_bytes`
  - `chipmunk_ring_signature_from_bytes`
- `module/crypto/src/sig/chipmunk/chipmunk_ring.h`
  - публичные прототипы сериализации подписи
- `module/crypto/src/sig/chipmunk/chipmunk_ring_serialize_schema.h`
  - утилиты сериализации сигнатуры
- `tests/unit/crypto/chipmunk_ring/test_chipmunk_ring_implementation_correctness.c`
  - проверки `NULL`-кейсов, roundtrip и корректности размеров
- `doc/crypto/chipmunk_ring/security_review_cr3_quantum_resilience_plan.md`
  - зависимые риски и контекст задач

## 2) Проверяемые риски

- **R-04**: dereference invalid input в API сериализации подписи (`to_bytes`/`from_bytes`) => возможный crash.
- **R-03**: неконсистентные размеры сигнатуры (`wire size` vs `roundtrip size`) и некорректная обработка усеченных данных.
- **R-04**: утечка состояния при ошибках десериализации (`parsing path`), если часть структуры была частично инициализирована до ошибки.

## 3) Набор тестов CR-4

### TC-01: Проверка fail-safe поведения `to_bytes`/`from_bytes`

- `to_bytes(NULL, ...)` и `from_bytes(NULL, ...)` возвращают ошибку.
- `to_bytes(..., NULL, ...)` и `from_bytes(..., NULL, ...)` возвращают ошибку.
- вход с `size == 0` возвращает ошибку.

### TC-02: Roundtrip сериализация/десериализация

- Создать валидную `dap_sign_t` ring-подпись через `dap_sign_create_ring`.
- Получить wire-blob через `dap_sign_get_sign`.
- Протестировать `chipmunk_ring_signature_from_bytes` на исходном blob.
- Выполнить `chipmunk_ring_signature_to_bytes` обратно в новый буфер.
- Сравнить исходный и восстановленный blob байт-в-байт.

### TC-03: Контроль размера output-сегмента

- Проверить, что для корректной десериализованной структуры undersized buffer (`size - 1`) приводит к ошибке сериализации.

### TC-04: Контроль частичных входов

- Проверить, что `from_bytes(..., size - 1)` (усеченные данные) завершается ошибкой без падения процесса.

## 4) Критерии прохождения

- Никаких падений процесса на invalid inputs.
- Восстановленный wire-blob эквивалентен исходному.
- Усеченный input/undersized output стабильно возвращают ошибку.
- Критические ошибки корректно логируются и детерминированы.
