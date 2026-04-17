# Chipmunk Ring Security Review — CR-0 (подготовка и рамка)

## Цель CR-0

Подготовить формальную рамку security review Chipmunk Ring Signature перед запуском CR-1..CR-5.

Критерий завершения CR-0:

- есть единый Threat model и модель доверия;
- определена поверхность атаки по модулю `chipmunk_ring*`, `dap_sign*`, `dap_serialize*`;
- утверждена risk matrix с owner и целевой фазой для снижения;
- подготовлены acceptance-пакеты и чек-листы для CR-1..CR-5;
- все зависимые задачи имеют явные входные критерии для запуска.

## 1) Контекст и границы доверия

### Внутренние доверенные границы (In Scope)

- Криптографические примитивы и параметры в `module/crypto/src/sig/chipmunk/`:
  - `chipmunk_ring.c`, `chipmunk_ring.h`, `chipmunk_ntt.c`, `chipmunk_poly.c`
  - `chipmunk_ring_acorn.c`, `chipmunk_ring_secret_sharing.c`, schema-файлы
- API интеграции:
  - `module/crypto/include/dap_enc_chipmunk_ring.h`
  - `module/crypto/include/dap_sign.h`
  - `module/crypto/include/dap_enc_chipmunk_ring_params.h`
- API glue и key lifecycle:
  - `module/crypto/src/dap_enc_chipmunk_ring.c`
  - `module/crypto/src/dap_sign.c`
  - `module/crypto/src/dap_enc_key.c`
- Сериализация и схема:
  - `module/core/include/dap_serialize.h`
  - `module/core/src/dap_serialize.c`
  - `module/crypto/src/sig/chipmunk/chipmunk_ring_serialize_schema.{h,c}`
- Тесты:
  - `tests/unit/crypto/chipmunk/*`
  - `tests/CMakeLists.txt`, соответствующая инфраструктура запуска

### Внешние зависимости (Out of Scope для CR-0)

- Атаки на саму модель угроз на уровне математических предположений Ring-LWE в целом (квантовая модель в CR-3);
- Сторонние компоненты runtime (allocator, ОС, аппаратная изоляция памяти и т.п.);
- Операционные процедуры эксплуатации (HSM, IAM, ротация ключей вне процесса DAP SDK).

## 2) Модель угроз (Actors → цели → точки входа)

### Акторы и цели

- A1. Наблюдатель/внешний злоумышленник:
  - цель: нарушить целостность подписи, подменить данные, вызвать аварии/крах;
  - векторы: управление входами API, подстановка malformed-подписей, фальшивый ring.
- A2. Злоумышленник с правами Ring participant:
  - цель: утечка приватных данных о signer или зависимостях в много-participant сценариях;
  - векторы: многократные запросы подписи/верификации, наблюдение за side-channel, манипуляция logging.
- A3. Компрометированный/некорректный участник сети (validator/relay):
  - цель: рассинхронизация состояния, неконсистентность ring_hash/ключей между узлами;
  - векторы: рассылка несовместимых serialized пакетов, reorder/replay/duplicate.
- A4. Ключевой/инфраструктурный атакующий:
  - цель: компрометация секретов и подмена RNG/seed.
- A5. Криптографический противник (Post-Quantum):
  - цель: проверка соответствия заявленных уровней сопротивляемости и параметров уровням.

### Скрытые предпосылки

- Предполагается корректная безопасность процесса инициализации и корректно настроенный модуль сборки/подписи.
- Предполагается, что приватные ключи предоставляются только доверенными путями, но функции API обязаны валидировать все параметры и отказывать на некорректных данных.
- Предполагается корректность платформенных криптографических примитивов `dap_*`, но в CR-0 мы фиксируем и верифицируем все входы в их адаптера.

## 3) Поверхность атаки

### 3.1 API layer (критичная)

1. `dap_enc_chipmunk_ring_key_new_generate`, `dap_enc_chipmunk_ring_sign`,
   `dap_enc_chipmunk_ring_get_signature_size`, инициализация (`init`).
2. `dap_sign_create_ring`, `dap_sign_verify_ring`, типы `SIG_TYPE_CHIPMUNK_RING`.
3. `dap_enc_chipmunk_ring_set/get_security_level`, `chipmunk_ring_set_security_preset`.
4. `chipmunk_ring_*` helpers, которые управляют сериализацией, выбором пресетов и `dap_ntt_*` path.

### 3.2 Data/serialization layer

1. Обработка `dap_sign_t`, `chipmunk_ring_signature_t`, внутренние schema поля и dynamic-size callbacks.
2. Любые переходы “wire ↔ memory”: `dap_serialize_to_buffer`, `dap_serialize_from_buffer`, `*_raw`.
3. Проверка `a_data_size`, `a_ring_size`, `a_signer_index`, размеров буфера signatures/pkeys.

### 3.3 Integration layer

1. Формирование ring для транзакций/consensus (`cellframe integration`).
2. Формат хранения и передачи `ring_hash`, метаданных signature и ключей.
3. Логирование и диагностические поля, которые могут косвенно влиять на анонимность.

### 3.4 Test/CI layer

1. Негативные кейсы и fuzz-входы.
2. Sanity ASAN/UBSAN (память/поведение UB).
3. Наличие deterministic-сборок и кроссплатформенной репродуцируемости.

## 4) Threat model → risk map

### 4.1 Риск-матрица (severity × likelihood)

| ID | Threat | Область | Severity | Likelihood | Impact | Owner | Фаза |
|---|---|---|---|---|---|---|---|
| R-01 | Некорректная валидация входных параметров (NULL/size/out-of-range) | `dap_sign` + `dap_enc_chipmunk_ring` API | High | High | Подмена результатов, DoS | CR-1 | CR-1 |
| R-02 | Интеграционные расхождения в `ring_size`/`signer_index` | `dap_sign_create_ring` | High | Medium | Некорректная верификация/ложные отказы | CR-1 | CR-1 |
| R-03 | Неконсистентная сериализация переменного размера | `dap_serialize*`, chipmunk schema | High | Medium | Потеря совместимости, подмена данных | CR-4 | CR-4 |
| R-04 | UAF/OOB в lifecycle ключей/подписей | `dap_enc_key`, `dap_sign_t` lifecycle | Critical | Low-Medium | Краш процесса, возможная компрометация | CR-4 | CR-4 |
| R-05 | Утечки через logging/debug/error paths | `dap_enc_chipmunk_ring*`, tests | Medium | Medium | Корреляционные утечки signer | CR-2 | CR-2 |
| R-06 | Отсутствующая/ошибочная валидация параметров PQ режима | `chipmunk_ring.c`, `...params` | High | Medium | Неверный security claim (M/V+) | CR-3 | CR-3 |
| R-07 | Несовместимость SIMD/dispatch реализаций NTT | `chipmunk_ntt.c` | Medium | Low | Непредсказуемая верификация на платформах | CR-4 | CR-4 |
| R-08 | Неточности в пороговой модели анонимности и формировании ring_hash | `chipmunk_ring.c`, Acorn/secret sharing | High | Low | Анонимность/линк-контроль ухудшены | CR-2 | CR-2 |
| R-09 | Отсутствие воспроизводимости измерений (perf/security метрик) | tests/benchmark + квантовые метрики | Low-Medium | Medium | Ошибочные решения по безопасности | CR-3, CR-5 | CR-3 |

### 4.2 Нормативный порог риска

- **Critical/High** → обязательные фиксы до PASS соответствующей фазы.
- **Medium** → фиксается в findings, либо доказательство отсутствия эксплуатируемости.
- **Low** → фиксируется в техническом debt log или при планировании очередного релиза.

## 5) Acceptance pack (для CR-1..CR-5)

### 5.1 Общие артефакты

- `security_review_cr0_preparation_pack.md` (этот документ).
- Привязка артефактов к задачам CR-1..CR-5 (`task_a85bc7e5`, `task_a68dc15c`, `task_e62c90dc`, `task_3e2ac592`, `task_6366a123`).
- Единый findings template + чек-лист (см. `custom_b665f1b65b79a43e`).

### 5.2 CR-1 (classical vulnerabilities)

- Набор негативных кейсов на входные размеры и pointer/ownership path.
- ASAN/UBSAN-гонка на core API + тесты parse/serialize.
- Результат: отчет по UAF/OOB/overflow с reproducible steps.

### 5.3 CR-2 (anonymity)

- План проверок по утечкам корреляции: логирование, формат ошибок, edge-case signatures.
- Сценарии повторной подписи и малых/повторяющихся колец.
- Контроль side-channel релевантности для публичных сообщений.

### 5.4 CR-3 (PQ resistance)

- Проверка пресетов уровней I/III/V/V+ через единый API (`set_security_level`/`get_security_level`).
- Воспроизводимые расчёты квантовых метрик и их документированное согласование с принятыми допущениями.
- Публичный mapping: target NIST уровень ↔ параметры/размеры.

### 5.5 CR-4 (implementation correctness)

- Проверка инвариантов `sign/verify`, `serialize/deserialize`, кросс-платформенной консистентности.
- Повторяемые тест-кейсы на целевых платформах, включая SIMD-диспатч.

### 5.6 CR-5 (closing)

- Консолидация findings, устранение/triage high+,
- Финальный security report + план remediations и owner matrix.

## 6) Гейтинг для перехода к CR-1

CR-0 считается `PASS`, когда выполнены пункты:

1. Threat model + attack surface зафиксированы и утверждены.
2. Risk matrix согласована с owners и фазами.
3. Acceptance pack по CR-1..CR-5 заполнен конкретными outputs.
4. Нет критических противоречий между API-ограничениями и тестовым матрицами.
5. `task_5fb13fe4` может быть переведена в статус "PASS" и передана в `task_a85bc7e5`.

## 7) Контрольные предположения (для следующих фаз)

- Наличие корректного RNG интерфейса на уровне `dap_random_*`.
- Безопасное хранение/удаление приватных ключей вне этого review.
- Любые изменения, влияющие на параметры безопасности или сериализацию, требуют обратной синхронизации `technical_specification.md` и `api_reference.md`.
