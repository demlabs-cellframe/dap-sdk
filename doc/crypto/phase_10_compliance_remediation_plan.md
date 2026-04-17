# Phase 10 — Architecture & SIMD Compliance Remediation

Этот документ является рабочим треком для нового этапа после закрытия оптимизационной трассы Phase 9 по решению пользователя.

## Цель Phase 10

- Закрыть технический долг по архитектурной совместимости SIMD/DSP и dispatch-механизмам.
- Привести high-priority файлы в соответствие политике `dap_tpl + dap_tpl/cmake_dispatch` без изменения криптографических параметров.
- Закрепить воспроизводимую baseline-процедуру для сравнений performance/correctness.

## 10.1 — Инвентаризация (High-priority, текущий шаг)

Состояние классифицируется как `pending`, `in_progress`, `done`, `defer`.

### Набор файлов для первоочередной проверки

| № | Файл | Категория | Текущий статус | Целевое действие | Ответственный/заметки |
|---|---|---|---|---|---|
| 1 | `hash/include/dap_hash_keccak.h` | high | pending | Миграция/валидация inline `ifdeffy` в DAP dispatch (`DAP_DISPATCH_*`) |  |
| 2 | `hash/include/dap_hash_keccak_x4.h` | high | pending | Проверка inline-веток NEON/x86, вынос в arch/`tpl` путь |  |
| 3 | `hash/include/dap_hash_shake128.h` | high | pending | Унификация dispatch через инфраструктуру, убираем x86-only direct пути |  |
| 4 | `hash/include/dap_hash_shake256.h` | high | pending | Унификация dispatch через инфраструктуру, убираем x86-only direct пути |  |
| 5 | `hash/src/dap_hash_sha3.c` | high | pending | Проверить наличие прямых fast-path веток и привести к unified dispatch |  |
| 6 | `hash/src/keccak/dap_keccak_x4_transpose_avx2.c` | high | pending | Проверить, что это не разрывает архитектурную политику, приоритет: `arch/` + `tpl` |  |
| 7 | `crypto/src/kem/mlkem/dap_mlkem_symmetric.h` | high | pending | Убрать x86-специфичные пути не через шаблонный механизм |  |
| 8 | `crypto/src/kem/mlkem/dap_mlkem_params.h` | high | pending | Проверить `target_clones` и заменить на инфраструктуру runtime |  |
| 9 | `crypto/src/kem/mlkem/dap_mlkem_ntt.c` | high | pending | Внедрить/проверить `DAP_DISPATCH_*` и корректные fallback-пути |  |
|10 | `crypto/src/sym/chacha20/dap_chacha20_internal.h` | high | pending | Убрать arch-gated declarations в пользу dispatch headers |  |
|11 | `crypto/src/sym/chacha20/dap_chacha20_poly1305.c` | high | pending | Проверить dispatch/inline-логики |  |
|12 | `crypto/src/sig/dilithium/dilithium_polyvec.c` | high | pending | Перевести inline AVX2/AVX512/NEON ветки в шаблоны и class dispatch |  |
|13 | `crypto/src/sig/dilithium/dilithium_poly.c` | high | pending | Перевести branching на DAP-инфраструктуру |  |
|14 | `crypto/src/sym/aes/dap_enc_aes.c` | high | pending | Проверка platform ifdef wiring |  |
|15 | `crypto/src/sym/aes/dap_aes_ni.c` | high | pending | Убедиться, что AES-NI путь корректно проходит через централизованный dispatch |  |
|16 | `crypto/src/sym/aes/dap_aes_armce.c` | high | pending | Аналогично для ARM CE |  |

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

- Зафиксировать baseline baseline-команды в одном документе (`build + benchmarks + parser`).
- Переходить к изменениям только после того, как baseline-пайплайн воспроизводим на Linux/arm64 runner’ах.

## 10.4 — Закрытие

- Обновить summary-файл task_5828bd04 с финальными статусами 10.1..10.4.
- Перенести уже закрытый фрагмент `Phase 9.1` в archival reference и пометить как historical.
