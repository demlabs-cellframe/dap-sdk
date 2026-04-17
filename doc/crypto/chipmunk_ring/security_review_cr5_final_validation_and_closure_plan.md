# CR-5 Security Review (Chipmunk Ring): финальная валидация и закрытие

## Цель CR-5

Завершить security review chipmunk-ring после CR-1..CR-4, зафиксировать итоговые прогоны и закрыть найденные дефекты в CR-5 scope.

## 1) Критерии финального закрытия

- Все целевые CR-1..CR-4 тесты и новые проверки проходят в единичном прогоне и в агрегированном наборе chipmunk-тестов.
- Оставшиеся инциденты CR-уровня должны быть устранены или явно триажированы с owner и планом.
- Результаты фиксаций и прогона должны быть задокументированы в рамках task_6366a123.

## 2) Покрытие CR-5

- `module/crypto/src/dap_sign.c`
  - `dap_sign_verify_ring` — hardening параметров валидации `a_ring_size`
- `tests/unit/crypto/chipmunk_ring/test_chipmunk_ring_input_validation.c`
  - правка нестабильного assert для invalid-ключевого типа (устранение ложной зависимости на уникальность кода ошибки)
- `tests/unit/crypto/chipmunk_ring/test_chipmunk_ring_implementation_correctness.c`
  - roundtrip и edge-case проверки на undersized/truncated inputs
- `tests/unit/crypto/chipmunk_ring/test_chipmunk_ring_security_levels.c`
  - покрытие CR-3 параметров безопасности (последующая валидация CR-5)
- `tests/CMakeLists.txt`
  - регистрация новых/существующих chipmunk-тестов в общем ctest-run

## 3) Риски, закрытые в CR-5

### R-05: Potential OOB read in verify path
- **Описание:** `dap_sign_verify_ring` принимал `a_ring_size > 0`, но не проверял верхнюю границу и мог читать `a_ring_keys[i]` за пределами массива при некорректном `a_ring_size`.
- **Статус:** **исправлено**
- **Фикс:**
  - добавлен guard `a_ring_size >= 2 && a_ring_size <= CHIPMUNK_RING_MAX_RING_SIZE` в `dap_sign_verify_ring` (ранняя валидация).
- **Файл:** `module/crypto/src/dap_sign.c` (`L1787`).

### R-06: Ошибка в проверке устойчивости error-path test
- **Описание:** тестовый сценарий сравнивал коды ошибок как индикатор различия путей, что приводило к ложному падению при валидных, но одинаковых кодах.
- **Статус:** **исправлено в тесте**
- **Фикс:** оставлена проверка «неуспешность ошибки», убрана неосновательная проверка различия кодов.
- **Файл:** `tests/unit/crypto/chipmunk_ring/test_chipmunk_ring_input_validation.c`.

## 4) Команды финальной валидации

```bash
cd /mnt/store/work/dap-sdk
mkdir -p build && cd build && cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build build -j$(nproc) --target test_unit_crypto_chipmunk_ring_input_validation
cmake --build build -j$(nproc) --target test_unit_crypto_chipmunk_ring_implementation_correctness
ctest --output-on-failure -R "test_unit_crypto_chipmunk_ring_" -j 8
```

## 5) Результаты прогона

- `test_unit_crypto_chipmunk_ring_input_validation` — `PASSED`
- `test_unit_crypto_chipmunk_ring_implementation_correctness` — `PASSED`
- Регрессионный батч `ctest -R "test_unit_crypto_chipmunk_ring_" -j 8` — `12/12` тестов `PASSED`

## 6) Критерий PASS для CR-5

CR-5 считается закрытым при:
- зеленом прогоне всех chipmunk unit-тестов;
- отсутствии падений на negative-input paths после исправлений;
- фиксации найденного R-05 и R-06 в коде/тестах.
