# Phase 10 — Architecture & SIMD Compliance Remediation

Этот документ является рабочим треком для нового этапа после закрытия оптимизационной трассы Phase 9 по решению пользователя.

## Цель Phase 10

- Закрыть технический долг по архитектурной совместимости SIMD/DSP и dispatch-механизмам.
- Привести high-priority файлы в соответствие политике `dap_tpl + dap_tpl/cmake_dispatch` без изменения криптографических параметров.
- Закрепить воспроизводимую baseline-процедуру для сравнений performance/correctness.

## 10.1 — Инвентаризация (High-priority, завершено)

Состояние классифицируется как `pending`, `in_progress`, `done`, `defer`.

### Набор файлов для первоочередной проверки

| № | Файл | Категория | Текущий статус | Целевое действие | Ответственный/заметки |
|---|---|---|---|---|---|
| 1 | `hash/include/dap_hash_keccak.h` | high | done | `DAP_DISPATCH_*` уже используется для `dap_hash_keccak_permute`; проверка только валидации платформенных прототипов | Архитектурно корректно для ядра, проверить Apple/SVE ветви в runtime smoke |
| 2 | `hash/include/dap_hash_keccak_x4.h` | high | done | Перенос на `DAP_DISPATCH_DECLARE_RESOLVE` + `DAP_DISPATCH_INLINE_CALL` для `xor_bytes_all`/`extract_bytes_all`/`permute`; убраны ручные рантайм-ветки и `static tier` кеши | Доработано в рамках 10.2 (runtime policy теперь унифицирована) |
| 3 | `hash/include/dap_hash_shake128.h` | high | done | Обёртка через `dap_keccak_sponge_get_ops()`, прямых `#if`/`ifdeffy` нет | Оставить как есть |
| 4 | `hash/include/dap_hash_shake256.h` | high | done | Обёртка через `dap_keccak_sponge_get_ops()`, прямых `#if`/`ifdeffy` нет | Оставить как есть |
| 5 | `hash/src/dap_hash_sha3.c` | high | done | Реализация чисто через ops API и без arch-гейтов | Оставить как есть |
| 6 | `hash/src/keccak/arch/x86/keccak_x4_transpose_avx2.tpl` | high | done | Перенесён в arch/tpl-пайплайн: template + `dap_tpl_generate`, функции остаются только в backend TU и вызываются через `dap_keccak_x4_*` dispatch | Удалён ручной `*.c`-backend из корня `src/keccak`, сборка подключает сгенерированный x86 TU |
| 7 | `crypto/src/kem/mlkem/dap_mlkem_symmetric.h` | high | done | Переведен на `DAP_DISPATCH_DECLARE_RESOLVE`/`DAP_DISPATCH_INLINE_CALL` для absorb/squeeze по каждому rate; убраны ручные tier-кеши | Поведенчески эквивалентно, использует общий KECCAK_SPONGE класс |
| 8 | `crypto/src/kem/mlkem/dap_mlkem_params.h` | high | done | `target_clones` уже удалены, статические пути через текущий dispatch в коде | Оставить как есть |
| 9 | `crypto/src/kem/mlkem/dap_mlkem_ntt.c` | high | done | Использует `DAP_DISPATCH_*`, `DAP_DISPATCH_DEFAULT/ARCH_SELECT_FOR/ENSURE` + явные fallback | Оставить как эталон для других модулей |
|10 | `crypto/src/sym/chacha20/dap_chacha20_internal.h` | high | done | Убраны платформенные ветки объявлений и оставлены только dispatch-совместимые backend объявления |
|11 | `crypto/src/sym/chacha20/dap_chacha20_poly1305.c` | high | done | Переведён на `DAP_DISPATCH_*` для ChaCha20/Poly1305 runtime-путей; удалены ручные singleton-based ветки |
|12 | `crypto/src/sig/dilithium/dilithium_polyvec.c` | high | done | Удалены локальные `__attribute__((target(...)))` и ручные x86 ветки; оставлен единый dispatch-конвейер с ref-путём как fallback | Валидация поведения через существующие тесты планируется в 10.2/10.3 |
|13 | `crypto/src/sig/dilithium/dilithium_poly.c` | high | done | Приведён к единым платформенным условиям (`DAP_PLATFORM_ARM`) и очищен от лишнего `dap_cpu_detect` include; dispatch-контур сохранён | Поведение подтверждается существующей `dap_dilithium_poly` тест-подсеткой |
|14 | `crypto/src/sym/aes/dap_enc_aes.c` | high | done | Переведен на `DAP_DISPATCH_*` через `DAP_DISPATCH_DECLARE_RESOLVE` + `DAP_DISPATCH_INLINE_CALL_RET`, убраны ручные function pointers и `pthread_once` |
|15 | `crypto/src/sym/aes/dap_aes_ni.c` | high | done | Реализует конкретный backend; компиляция под x86 gated в `#if DAP_PLATFORM_X86` | Явный backend-модуль оставляем, но вызывается через dispatch |
|16 | `crypto/src/sym/aes/dap_aes_armce.c` | high | done | Реализует конкретный ARM CE backend; компиляция под ARM gated в `#if DAP_PLATFORM_ARM` | Явный backend-модуль оставляем, но вызывается через dispatch |

### 10.1.1 Результат скрининга (кратко)

- Идентифицировано 0 файлов, где требуется доработка `in_progress`.
- В `done`-статусе на этапе 10.1: `dap_hash_shake128.h`, `dap_hash_shake256.h`, `dap_hash_sha3.c`, `dap_mlkem_params.h`, `dap_mlkem_ntt.c`, `dap_hash_keccak.h`, `dap_hash_keccak_x4.h`, `dap_mlkem_symmetric.h`, `hash/src/keccak/arch/x86/keccak_x4_transpose_avx2.tpl`, `crypto/src/sym/chacha20/dap_chacha20_internal.h`, `crypto/src/sym/chacha20/dap_chacha20_poly1305.c`, `crypto/src/sig/dilithium/dilithium_polyvec.c`, `crypto/src/sig/dilithium/dilithium_poly.c`, `crypto/src/sym/aes/dap_enc_aes.c`, `dap_enc_aes` backend-файлы (`dap_aes_ni.c`, `dap_aes_armce.c`).
- Для всех `in_progress` файлов зафиксированы конкретные follow-up задачи в следующей подфазе 10.2.

### Средний и низкий приоритет (отложено на последующие итерации)

- Falcon, Streebog, JSON SIMD и `crypto/include/dap_crypto_common.h` — пометить для post-Phase-10.
- Medium/low приоритеты не должны блокировать начало 10.2.

## 10.2 — Ремедиация (high-priority)

### Порядок исполнения

1. Закрыть все `pending` файлы из таблицы по одному.
2. Для каждого файла в каждой итерации зафиксировать:
   - Что именно изменено,
   - К какому acceptance-критерию относится,
   - Какой тест/проверка выполнена (или причина откладывания).
3. Для каждого блока поддерживать минимальный «proof of correctness»:
   - существующие unit/crypto тесты,
   - regression run на соответствующий компонент,
   - `ctest`/целевые цели для измененного модуля.

## 10.3 — Метрики и воспроизводимость

Статус: `done`
- x86_64 Linux: `done` (конфигурирование + build + full `ctest` + `-L performance` + парсинг)
- arm64 runner: `done` (cross build + `-L performance` + парсинг)

### 10.3.1. Артефакт

- Добавлен единый baseline-руководство: `doc/crypto/phase_10_3_reproducibility_baseline.md`
- Добавлен parser-утилит: `scripts/phase10_parse_ctest_performance.sh`
- В обоих файлах зафиксирован reproducible-процесс: `configure -> build -> unit tests -> performance suite`.

### 10.3.2. Acceptance criteria

- Команда сборки и тестов запускается одной последовательностью без ручных пропусков.
- Производительность измеряется отдельно от unit regression.
- Результат сводится в машинно-парсируемый формат (label summary + итоговая длительность).
- Перед стартом следующего шага pipeline должен прогоняться на Linux (x86_64) и arm64 runner’ах без локальных обходов.

### 10.3.3. Рекомендуемый порядок (Linux arm64-friendly)

1. `cmake -S . -B build-phase10 -DCMAKE_BUILD_TYPE=Release -DBUILD_DAP_SDK_TESTS=ON`
2. `cmake --build build-phase10 -j$(nproc)`
3. `ctest --test-dir build-phase10 --output-on-failure`
4. `ctest --test-dir build-phase10 -L performance --output-on-failure`
5. `scripts/phase10_parse_ctest_performance.sh build-phase10/Testing/Temporary/LastTest.log`

Для arm64 runner запускается тот же набор шагов с `build-phase10-arm64` (или нативным кросс-билдом CI-пайплайна, если он применяется).

## 10.4 — Закрытие

Статус: `done`

- Создан closure-свод: `doc/crypto/phase_10_closure_summary.md` (статусы 10.1, 10.2, 10.3).
- Закрытый фрагмент `Phase 9.1` помечен как historical:
  - исходник: `doc/crypto/archive/phase_9_1_simd_dispatch_map_historical.md`
  - и сам файл `doc/crypto/phase_9_1_simd_dispatch_map.md` теперь содержит ссылку на архивную версию.

`task_5828bd04` финальный статус зафиксирован в `doc/crypto/phase_10_closure_summary.md`.
