# CR-1 Security Review (Chipmunk Ring): Классические уязвимости

## Цель CR-1

Системно проверить слой корректности и устойчивости к классическим (не-квантовым) дефектам:

- ошибки валидации параметров и границ,
- ошибки обработки ошибок/инициализации,
- ошибки владения памятью и жизненным циклом,
- инварианты сериализации/десериализации.

`CR-1` закрывается, только если закрыты/сформированы для фиксации все high/critical для классов R-01..R-03 из CR-0.

## 1) Пакет проверки (execution pack)

### 1.1 Покрытие исходных файлов

- `module/crypto/src/dap_sign.c`
  - `dap_sign_create_ring`
  - `dap_sign_verify_ring`
  - пути верификации и ошибок API
- `module/crypto/src/dap_enc_chipmunk_ring.c`
  - lifecycle key/sign helpers
  - `dap_enc_chipmunk_ring_sign`
  - `dap_enc_chipmunk_ring_get_signature_size`
- `module/crypto/src/sig/chipmunk/chipmunk_ring.c`
  - инициализация/создание контейнеров/ключей
  - подписывание и проверка
  - валидация параметров
- `module/crypto/src/sig/chipmunk/chipmunk_ring_serialize_schema.c`
  - условные/размерные функции сериализации
- `module/core/src/dap_serialize.c`
  - проверка пересчёта/сериализации/десериализации
- `module/crypto/src/dap_enc_key.c`
  - регистрация и удаление callbacks для `DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING`

### 1.2 Проверочные цели

- R-01: некорректная валидация входных параметров (NULL, overflow/underflow, out-of-range sizes).
- R-02: расхождения в согласованности `ring_size`/`signer_index`, несогласованные состояния ключей.
- R-03: инварианты сериализации переменного размера и схема dynamic fields.

## 2) Набор контрольных тестов

Ниже — минимально достаточная матрица для CR-1; расширяется в процессе прогонки:

### TC-01..TC-06: API-грубые проверки

- `TC-01`: `dap_sign_create_ring(NULL, ...)` => строгое `-EINVAL` без segfault.
- `TC-02`: `a_ring_size < 2` и `a_ring_size > CHIPMUNK_MAX_RING_SIZE` для `create/verify`.
- `TC-03`: `signer_index >= a_ring_size` для подписания — ожидается ошибка, без чтения памяти.
- `TC-04`: `a_ring_keys == NULL` при `a_ring_size > 0` -> ошибка.
- `TC-05`: `a_data_size` с граничными значениями (0, 1, очень большой near size_t_MAX/2) при наличии валидного ring.
- `TC-06`: `ring_size == 0` в `dap_sign_verify_ring`/`dap_enc_chipmunk_ring_get_signature_size`.

### TC-07..TC-12: memory safety

- `TC-07`: двойной `free/delete` и повторное использование `dap_sign_t*` после ошибки.
- `TC-08`: частичный сбой внутри цепочки `chipmunk_ring_sign`/`chipmunk_ring_verify` (моделируемый через fault injection points, если есть).
- `TC-09`: переполнение буфера при вычислении размеров (проверить переполнение промежуточных умножений при `ring_size` near max).
- `TC-10`: стресс многопоточной выдачи/верификации без явной синхронизации stateful данных.
- `TC-11`: корректное поведение API на невалидном/грязном `dap_enc_key_t` (пустой/сломанный payload).
- `TC-12`: проверка "no memory leak" для коротких циклов create/verify на множественных итерациях.

### TC-13..TC-17: serialize compatibility

- `TC-13`: roundtrip `dap_serialize_to_buffer()` → `dap_serialize_from_buffer()` для типовых `chipmunk_ring_signature_schema`.
- `TC-14`: roundtrip через `dap_serialize_raw_*` и сравнение байтовой длины/детерминизма.
- `TC-15`: сериализация с искусственно неконсистентным `ring_size` в blob (ожидается ошибка валидации, не UB).
- `TC-16`: совместимость между разными dispatch-путями NTT (при наличии runtime switch), в том числе на тестовых компиляциях.
- `TC-17`: fuzz-гамма на blob’ах signature/pkey, где `size` поля намеренно некорректны.

## 3) Санитайзерная обязательная матрица

Для CR-1 обязательно выполнить:

- `ASAN` и `UBSAN` в минимальном и stress-конфиге по:
  - `test_unit_crypto_chipmunk_hots` / текущий существующий chipmunk тест-набор,
  - добавленным негативным CR-1 кейсам.
- Проверить отсутствие:
  - heap-buffer-overflow,
  - stack-overflow из-за размера/offset,
  - invalid-free/use-after-free,
  - signed integer overflow в подсчёте длины и offsets.

## 4) Логический драфт PR/вывода для CR-1

По каждому тест-кейсу и каждому найденному отклонению фиксируются поля:

- `file:function` (место)
- `artifact` (тест / crash dump / sanitizer output)
- `impact`: local / cross-module
- `likelihood`: всегда/иногда/редко
- `severity`: critical/high/medium/low
- `fix`:
  - guard/path fix,
  - memory boundary correction,
  - explicit fail-fast policy.

### Фиксация результатов

- Все failures, даже non-blocking, оформляются в template из `custom_b665f1b65b79a43e` (findings).
- Для high/critical — безусловный переход в fix или explicit mitigation plan с owner.

## 5) Acceptance criteria CR-1

- Нет репродуцируемых OOB/NULL-deref/UAF/invalid-free на API и serial path.
- Все R-01..R-03 закрыты либо triage’ированы с документированным статусом и owner до PASS.
- ASAN/UBSAN не содержат "стабильных" регрессий в зоне CR-1.
- Добавлены/добавлены-готовыми к add-у негативные тесты для top-10 high-risk сценариев из этой фазы.

## 6) Результат для CR-1

- `PASS CR-1`: после обновления `task_a85bc7e5` с подтверждением, что CR-2 может стартовать.
- `FAIL CR-1`: если любой из high/critical остаётся без доказанного фикса/triage plan.

## 7) Следующий шаг после CR-1

После PASS CR-1 запускается `task_a68dc15c` (CR-2, анонимность и утечки).
