# ISSUE: ASAN/UBSAN конфигурация не доводится до линковки тестов

## Кратко
При `DAP_ASAN=1` сборка тестов падает с `undefined reference to __asan_*`.  
Санитайзер-флаги добавляются в `CMAKE_C_FLAGS`/`CMAKE_LINKER_FLAGS`, но итоговые тестовые бинарники линкуются через C++ linker и не всегда получают корректные sanitizer link options.

## Затронутый код
- `cmake/OS_Detection.cmake:140` (`-fsanitize=address` в `_CCOPT`)
- `cmake/OS_Detection.cmake:143` (`-fsanitize=address` в `_LOPT`)
- `cmake/OS_Detection.cmake:258` (`set(CMAKE_C_FLAGS ...)`)
- `cmake/OS_Detection.cmake:259` (`set(CMAKE_LINKER_FLAGS ...)`)

## Симптом
При сборке с `DAP_ASAN=1`:
- линковка тестов падает;
- типичный текст ошибки: `undefined reference to '__asan_*'`.

## Ожидаемое поведение
- Любой тестовый бинарник в sanitizer-конфигурации должен успешно линковаться с runtime санитайзера.

## Фактическое поведение
- Санитайзер-прогон по `io/worker/proc_thread` заблокирован на этапе build/link.

## Риск
- Medium: теряется ключевой слой обнаружения UB/heap bugs/race-like corruption в CI и локальной валидации.

## Предложение по исправлению
- Перейти с глобальных строковых флагов на target-level API:
  - `target_compile_options(... -fsanitize=...)`
  - `target_link_options(... -fsanitize=...)`
- Применять sanitizer options ко всем тестовым target (`add_dap_test`) и библиотекам, которые с ними линкуются.
- Добавить smoke-check в CI: минимальный sanitizer build + запуск 1-2 тестов.

