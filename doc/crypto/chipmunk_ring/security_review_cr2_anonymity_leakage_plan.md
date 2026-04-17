# CR-2 Security Review (Chipmunk Ring): Анонимность и утечки

## Цель CR-2

CR-2 закрывает риск-области `R-08` и `R-05` из `security_review_cr0_preparation_pack.md`:
- проверить, нет ли практических каналов утечки signer identity/корреляционной информации;
- исключить утечки метаданных через диагностические и отладочные траектории;
- зафиксировать матрицу тестов по leakage-поверхности.

CR-2 считается `PASS`, если все high/critical риски по анонимности и утечкам закрыты и оформлены в findings или triage с owner.

## 1) Область проверки

### In-scope
- `module/crypto/src/sig/chipmunk/chipmunk_ring.c`
  - логирование критичных этапов `sign`/`verify`
  - поиск реального подписанта (`l_real_signer_index`) и обработка подписей
  - формирование challenge и проверочный путь
- `module/crypto/src/dap_enc_chipmunk_ring.c`
  - логирование при сборке ring-контекста (`a_ring_pub_keys`) и подготовке `ring hash`
- `tests/unit/crypto/chipmunk_ring/test_chipmunk_ring_anonymity.c`
- `tests/security/test_ring_signature_zkp.c`

### Out-of-scope (пока)
- математическая безопасность схемы (квантовые параметры: CR-3)
- анализ аппаратных side-channel (cache/power, Spectre-like)

## 2) Дополнительные риски для CR-2

| ID | Риск | Точка входа | Severity | Likelihood | Признак/контроль |
|---|---|---|---|---|---|
| R-2A | Утечки по диагностическим логам (публичный signer/временные отпечатки) | `debug_if`/`log_it` в `chipmunk_ring.c`, `dap_enc_chipmunk_ring.c` | Medium | Medium | Поиск логов с signer индексом, challenge, ring hash bytes |
| R-2B | Корреляционные отличия подписей разных signers | output signature shape по участнику | Medium | Medium | Проверка статистики различимости метаданных/размеров |
| R-2C | Поведенческая утечка через разные ветки ошибок в `verify` | `dap_sign_verify_ring`/`chipmunk_ring_verify` | Low-Medium | Medium | Анализ сообщений и последовательностей ошибок (только внутренне) |

## 3) План тестирования CR-2

### 3.1 Логи и метаданные
- `TC-01`: Проверить, что debug-mode по умолчанию не раскрывает bytes signer-специфичных полей (`ring hash`, `challenge`, first signature bytes).
- `TC-02`: Проверить, что строки об индексах signer/ключах не появляются в публичных/типовых логах (включая INFO/ERROR путь в обычном runtime).
- `TC-03`: Подтвердить, что включение детального логирования не меняет API/валидность подписей, но убирает утечки чувствительных значений.

### 3.2 Анонимность и корреляция подписей
- `TC-04`: Для одного и того же `ring` и `message` сравнить подписи разных signers:
  - все подписи валидны;
  - `header.sign_size` одинаков для всех signer-ролей;
  - различение signer по размерам/детерминированным полям отсутствует на уровне статического анализа.
- `TC-05`: Для одного signer и одного сообщения убедиться в вариативности подписи при повторных вызовах (nonce/рандомизация активны).
- `TC-06`: Для разных сообщений тем же signer проверить отсутствие вырожденных детерминированных паттернов (по количеству полных совпадений).

### 3.3 Базовая утечка через ошибки валидации
- `TC-07`: Отдельно пройти невалидные входы `verify` и зафиксировать единообразие поведения:
  - NULL сигнатура
  - `ring_size` 0 / 1 / `CHIPMUNK_RING_MAX_RING_SIZE+1`
  - неверный `ring`/ключи
  - проверка, что ошибки остаются валидационными, без попыток доступа к приватным данным.

### 3.4 Риск-лист и артефакты
- Все инциденты или изменения логирования документировать в template findings.
- Для R-2A, если не удаётся устранить полностью — оформить как `triage` с explicit owner и сроком снижения.

## 4) Acceptance Criteria CR-2

- Нет явной утечки signer identity через стандартные/умолчательные логи.
- Для одной подписки (message + ring) нет trivially distinguishable метаданных по signer.
- Ошибки в `verify` и/или `create` не раскрывают sensitive offsets/содержимое challenge/public keys.
- Документированы все проверки CR-2 с clear pass/fail выводом для задачи `task_a68dc15c`.

## 5) Текущие действия

- На текущем шаге завершён первый этап hardening: убраны чувствительные дампы из `chipmunk_ring.c` и `dap_enc_chipmunk_ring.c`.
- TC-04 закрыт: расширение в `tests/unit/crypto/chipmunk_ring/test_chipmunk_ring_anonymity.c` проверяет подпись разными участниками `ring`, одинаковый `sign_size` и единый валидационный путь.
- TC-05 закрыт: для повторных подписей того же signer/message добавлена проверка неполной детерминированности (`memcmp` не всегда совпадает).
- TC-06 закрыт: существующий тестовый сценарий анонимности и `s_test_multi_message_anonymity` в `test_chipmunk_ring_anonymity.c` продолжает отрабатывать различимость подписей по разным сообщениям при сохранении валидности.
- TC-07 закрыт: добавлен и пройден негативный сценарий проверки консистентности путей ошибки `verify` (нулевая сигнатура, некорректные размеры, неверный тип ключа, oversize `ring_size`) в `test_chipmunk_ring_input_validation.c`.
- Все тесты CR-2/TC-04..TC-07 подтверждены в `build` и `build_ubsan_noerr`:
  - `test_unit_crypto_chipmunk_ring_basic`
  - `test_unit_crypto_chipmunk_ring_anonymity`
  - `test_unit_crypto_chipmunk_ring_stress`
  - `test_unit_crypto_chipmunk_ring_input_validation`
- Далее закрыть formal acceptance: обновить `task_a68dc15c` с итоговым pass/fail чек-листом и наблюдениями по residual risks.
