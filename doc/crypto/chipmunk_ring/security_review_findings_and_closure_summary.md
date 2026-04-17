# Chipmunk Ring: итоговая сводка Security Review CR-0..CR-5

## Цель

Завершить и консолидировать все этапы CR-0..CR-5 для `chipmunk_ring`, зафиксировать статус каждой фазы, закрытые и остаточные риски, ключевые правки кода и итоговый статус валидации.

## Результат по этапам (финальный)

| Этап | Статус | Ключевой результат |
|---|---|---|
| CR-0 | PASS | Threat model, attack surface и risk matrix сформированы и утверждены. |
| CR-1 | PASS | Добавлены/усилены проверки API на classic-кейсы: `NULL`, размеры, диапазоны и несогласованные входные параметры. |
| CR-2 | PASS | Проведена валидация анонимности и корреляционных утечек на уровне logs/error-path и поведения подписи. |
| CR-3 | PASS | Проверены security levels (I/III/V/V+), API параметров, downgrade-сценарии и детерминизм `security_info`. |
| CR-4 | PASS | Зафиксирована корректность `to_bytes/from_bytes`: `NULL`/некорректные размеры и edge-cases теперь отвергаются стабильно. |
| CR-5 | PASS | Финальная проверка закрыла ошибки в verify/input-validation и консолидацию результатов. |

## Закрытые риски по CR-0 matrix

### R-01: Некорректная валидация входных параметров
- Статус: fixed
- Артефакты:
  - `tests/unit/crypto/chipmunk_ring/test_chipmunk_ring_input_validation.c`
  - `module/crypto/src/dap_sign.c` (`dap_sign_create_ring`, `dap_sign_verify_ring`)

### R-02: Расхождения в согласованности ring_size/signer
- Статус: fixed
- Артефакты:
  - `tests/unit/crypto/chipmunk_ring/test_chipmunk_ring_input_validation.c`
  - проверки порогов и размеров в `chipmunk_ring` тест-матрице

### R-03: Неконсистентная сериализация переменного размера
- Статус: fixed
- Артефакты:
  - `module/crypto/src/sig/chipmunk/chipmunk_ring.c` (`chipmunk_ring_signature_to_bytes`, `chipmunk_ring_signature_from_bytes`)
  - `tests/unit/crypto/chipmunk_ring/test_chipmunk_ring_implementation_correctness.c`

### R-04: UAF/OOB в lifecycle
- Статус: reduced
- Артефакты:
  - усиленные проверки ключей и ранний выход из `dap_sign_verify_ring`
  - расширенные негативные тесты в `test_chipmunk_ring_input_validation.c`

### R-05: Утечки через debug/log и error-path
- Статус: mitigated
- Артефакты:
  - `doc/crypto/chipmunk_ring/security_review_cr2_anonymity_leakage_plan.md` (закрытие TC-04..TC-07)
  - тесты по анонимности и randomization в `tests/unit/crypto/chipmunk_ring/test_chipmunk_ring_anonymity.c`

### R-06: Ошибки параметризации и downgrade
- Статус: fixed
- Артефакты:
  - `tests/unit/crypto/chipmunk_ring/test_chipmunk_ring_security_levels.c`
  - API уровня безопасности и маппинга в `module/crypto/src/sig/chipmunk/chipmunk_ring.c`

### R-07: Непоследовательность SIMD/NTT dispatch
- Статус: deferred
- Артефакты:
  - отдельный этап optimization (Phase 9) сейчас отложен по решению пользователя.
  - для этого этапа подготовлена отдельная карта входов в `doc/crypto/phase_9_1_simd_dispatch_map.md`

### R-08: Модель анонимности и корреляционные риски
- Статус: mitigated
- Артефакты:
  - `doc/crypto/chipmunk_ring/security_review_cr2_anonymity_leakage_plan.md`
  - `tests/unit/crypto/chipmunk_ring/test_chipmunk_ring_anonymity.c`

### R-09: Воспроизводимость метрик/методик
- Статус: open (non-critical)
- Артефакты:
  - `doc/crypto/chipmunk_ring/quantum_threat_analysis.md`
  - требуется отдельный follow-up в задачах по измерениям и оптимизациям

## Ключевые закрытые правки кода

1. `module/crypto/src/dap_sign.c`
   - в `dap_sign_verify_ring` добавлен guard: `a_ring_size >= 2 && a_ring_size <= CHIPMUNK_RING_MAX_RING_SIZE`.
2. `module/crypto/src/sig/chipmunk/chipmunk_ring.c`
   - добавлены проверки на `NULL`/`0` и минимальный размер в `chipmunk_ring_signature_to_bytes` и `chipmunk_ring_signature_from_bytes`.
3. `tests/unit/crypto/chipmunk_ring/test_chipmunk_ring_input_validation.c`
   - унифицирована политика проверки негативных путей (`verify` возвращает failure без ложного сравнения кодов ошибки).
4. `tests/unit/crypto/chipmunk_ring/test_chipmunk_ring_implementation_correctness.c`
   - добавлены проверка undersized/truncated inputs и roundtrip invariants на корректном размере.

## Результаты тестирования

- `test_unit_crypto_chipmunk_ring_input_validation` — PASSED
- `test_unit_crypto_chipmunk_ring_implementation_correctness` — PASSED
- `ctest --output-on-failure -R "test_unit_crypto_chipmunk_ring_" -j 8` — 12/12 PASSED

### Команды для повторного прогона

```bash
cd /mnt/store/work/dap-sdk/build
ctest --output-on-failure -R "test_unit_crypto_chipmunk_ring_" -j 8
```

```bash
cd /mnt/store/work/dap-sdk/build
./tests/bin/test_unit_crypto_chipmunk_ring_input_validation
./tests/bin/test_unit_crypto_chipmunk_ring_implementation_correctness
```

## Закрытие triage

На момент фиксации CR-5 критических и high-инцидентов по Chipmunk Ring в CR-0..CR-5 не осталось.
Остаточные medium/low риски (`R-07`, `R-09`) помечены как follow-up после отдельного этапа оптимизации/метрик.
