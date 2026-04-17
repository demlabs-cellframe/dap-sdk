# CR-3 Security Review (Chipmunk Ring): квантовая устойчивость и параметры

## Цель CR-3

Проверить, что реализации параметров Chipmunk Ring действительно соответствуют заявленным уровням постквантовой стойкости, а выбранные параметры не допускают скрытого понижения защиты или неконсистентного расчета уровня.

CR-3 завершится только при подтверждении:
- корректности маппинга `CHIPMUNK_RING_SECURITY_LEVEL_*`;
- детерминированности/предсказуемости результата `dap_enc_chipmunk_ring_get_security_info`;
- отсутствии downgrade-векторoв через неверную инициализацию или невалидные параметры.

## 1) Пакет проверки

### 1.1 Покрытие файлов

- `module/crypto/include/dap_enc_chipmunk_ring.h`
  - `chipmunk_ring_security_level_t`
  - `chipmunk_ring_security_info_t`
  - API: `init_with_security_level`, `get_security_info`, `get_params_for_level`, `validate_security_level`, `set_params/reset_params`.
- `module/crypto/src/sig/chipmunk/chipmunk_ring.c`
  - Таблица `s_security_presets`
  - `dap_enc_chipmunk_ring_init_with_security_level`
  - `chipmunk_ring_set_params`
  - `dap_enc_chipmunk_ring_get_security_info`
  - `dap_enc_chipmunk_ring_validate_security_level`
- `module/crypto/include/dap_enc_chipmunk_ring_params.h`
  - значения `CHIPMUNK_RING_SECURITY_LEVEL_*`
  - связанные рассчитанные константы и лимиты размеров.
- `tests/unit/crypto/chipmunk_ring/test_chipmunk_ring_parameter_comparison.c` (как эталонные измерители по скорости/безопасности).
- `tests/unit/crypto/chipmunk_ring/*_validation*` (по необходимости расширение набора негативных кейсов).

### 1.2 Проверяемые риски

- **R-3A**: некорректное сопоставление уровня безопасности (заявленный уровень не соответствует фактическим параметрам `ring_lwe_n/ntru_n/code_*`).
- **R-3B**: fallback на более слабые параметры при невалидном уровне / неуспешном инициализаторе.
- **R-3C**: некорректный расчет квантовой стойкости (квант-битов и требований логических qubit).
- **R-3D**: отсутствие защиты от downgrade при ручной настройке параметров (возможность задать параметры ниже заявленного уровня без корректной ошибки/предупреждения).

## 2) Набор тестов CR-3

### TC-01: Инициализация по уровню

- `init_with_security_level(CHIPMUNK_RING_SECURITY_LEVEL_I/III/V/V_PLUS)` должен возвращать `0`.
- После инициализации `get_security_info` должен давать ожидаемый уровень и положительные значения (`classical_bits`, `quantum_bits`, `logical_qubits_required`).

### TC-02: Невалидный уровень

- `init_with_security_level(0)` и `init_with_security_level(999)` должны возвращать ошибку (`-EINVAL`).

### TC-03: Маппинг уровней

- Проверить монотонность параметров для уровней `I -> III -> V -> V+`.
- Проверить, что `get_params_for_level` для каждого уровня возвращает ожидаемые `ring_lwe_n`, `ntru_n`, `code_n` из кодовой таблицы.

### TC-04: Валидация нижнего порога

- `validate_security_level` после `init_with_security_level(III)` должен проходить для `I` и `III`, и падать для `V`/`V_PLUS`.

### TC-05: Downgrade/переход состояния

- Сформировать кастомные параметры с очень низкими `ring_lwe_n` и проверить:
  - `chipmunk_ring_set_params` принимается только для структурно валидных значений,
  - `validate_security_level(V)` возвращает ошибку,
  - после `reset_params` система возвращается в безопасный baseline.

### TC-06: Агрегированный квантовый профиль

- `get_security_info.logical_qubits_required` должен быть > 0 для рабочих параметров и непротиворечив после `set_params`/`reset_params`.

### TC-07: Ошибки интеграции параметров в API

- Проверить, что `get_params_for_level` возвращает `-EINVAL` для несуществующего уровня и не меняет внешнее состояние параметров.

## 3) Критерии прохождения CR-3

- Нет обнаруженных отклонений по рискам `R-3A..R-3D`, либо они triage’ированы с owner и планом фикса.
- API-контракт для уровней безопасности стабилен и воспроизводим (unit-тесты для всех TC проходят).
- Любой fallback/ошибка параметров не допускает скрытого понижения уровня без явной ошибки.

## 4) Текущие действия

- CR-2 закрыта и зафиксирована.
- Для CR-3:
  - необходимо оформить и добавить negative/positive coverage по security API в unit-тесты chipmunk ring;
  - зафиксировать результаты в `task_e62c90dc` после прогона и в `task_cb5aad6b` для findings-пакета.
