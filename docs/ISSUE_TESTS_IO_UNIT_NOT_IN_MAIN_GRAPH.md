# ISSUE: `tests/unit/io` не подключен в основной CTest-граф

## Кратко
В репозитории есть отдельные unit-тесты IO (`tests/unit/io/CMakeLists.txt`), включая `test_dap_worker` и `test_dap_proc_thread`, но они не добавляются из `tests/CMakeLists.txt`.

## Затронутый код
- `tests/CMakeLists.txt:129` (`add_subdirectory(unit/core)`)
- `tests/CMakeLists.txt:135` (`add_subdirectory(unit/json)`)
- Отсутствует аналогичный `add_subdirectory(unit/io)`.

## Симптом
- При обычном `ctest`/`ctest -L io` эти unit-тесты отсутствуют в стандартном графе исполнения.
- В результате покрытие worker/proc_thread unit-level сценариев неполное.

## Ожидаемое поведение
- `tests/unit/io/CMakeLists.txt` должен участвовать в общем `tests` build pipeline так же, как `unit/core` и `unit/json`.

## Фактическое поведение
- Исполняются только вручную добавленные regression-тесты из `tests/CMakeLists.txt`, а полноценный `unit/io` набор не подключен.

## Риск
- Medium: пропуск регрессий в IO public API и worker lifecycle на уровне unit-тестов.

## Предложение по исправлению
- Добавить в `tests/CMakeLists.txt`:
  - условный блок `if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/io/CMakeLists.txt)`
  - `add_subdirectory(unit/io)`
- Проверить labels/timeout для тестов из `tests/unit/io/CMakeLists.txt`, чтобы они фильтровались через `ctest -L io`.

