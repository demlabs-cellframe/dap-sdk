# Расширения шаблонов DAP для мокинга

**Внутренняя документация** - Для разработчиков, расширяющих mock framework

## Обзор

Расширения шаблонов DAP для мокинга предоставляют специализированные конструкции и функции переменных для генерации C кода, используемого в mock framework. Эти расширения находятся в `test-framework/mocks/lib/dap_tpl/` и загружаются через параметр `--extensions-dir`.

Расширения дополняют базовую систему шаблонов dap_tpl конструкциями, специфичными для C, и преобразованиями переменных, необходимыми для генерации кода моков.

### Использование

```bash
source dap_tpl/dap_tpl.sh

replace_template_placeholders \
    template.tpl \
    output.h \
    --extensions-dir "../mocks/lib/dap_tpl" \
    "FEATURE_NAME=ENABLE_DEBUG" \
    "TYPE=dap_list_t*"
```

### Конструкции

#### c_ifdef - Условная компиляция препроцессора C

Генерирует директивы препроцессора `#ifdef`, `#elifdef`, `#else`, `#endif`.

**Синтаксис:**
```
{{#c_ifdef FEATURE_NAME}}
#ifdef FEATURE_NAME
// код здесь
{{#c_elif OTHER_FEATURE}}
#elifdef OTHER_FEATURE
// альтернативный код
{{#c_else}}
#else
// код по умолчанию
{{/c_ifdef}}
#endif
```

**Пример:**
```
{{#c_ifdef ENABLE_DEBUG}}
#ifdef ENABLE_DEBUG
    log_debug("Debug mode enabled");
{{#c_else}}
#else
    // Release mode
{{/c_ifdef}}
#endif
```

**Особенности:**
- Автоматически удаляет лишние пустые строки после директив
- Правильно обрабатывает вложенные условия
- Поддерживает множественные `#c_elif` блоки

#### c_for - Циклы for в формате C

Генерирует C-массивы и инициализаторы с правильной обработкой запятых.

**Синтаксис:**
```
{{#c_for key,val in PAIRS}}
{ .key = {{key}}, .val = {{val}} },
{{/c_for}}
```

**Пример:**
```
{{#set PAIRS=name|string
id|int
count|size_t}}

static const struct {
    const char *key;
    const char *type;
} pairs[] = {
{{#c_for key,val in PAIRS}}
    { .key = "{{key}}", .type = "{{val}}" },
{{/c_for}}
};
```

**Результат:**
```c
static const struct {
    const char *key;
    const char *type;
} pairs[] = {
    { .key = "name", .type = "string" },
    { .key = "id", .type = "int" },
    { .key = "count", .type = "size_t" },
};
```

**Особенности:**
- Автоматически добавляет запятые между элементами
- Удаляет лишнюю запятую после последнего элемента
- Поддерживает вложенные структуры

#### c_struct - Генерация typedef struct

Генерирует определения структур C с автоматическим добавлением суффикса `_t`.

**Синтаксис:**
```
{{#c_struct StructName}}
int field1;
char* field2;
{{/c_struct}}
```

**Пример:**
```
{{#c_struct MockConfig}}
bool enabled;
int timeout_ms;
const char* name;
{{/c_struct}}
```

**Результат:**
```c
typedef struct {
    bool enabled;
    int timeout_ms;
    const char* name;
} MockConfig_t;
```

**Особенности:**
- Автоматически добавляет `typedef struct {...} StructName_t;`
- Сохраняет форматирование полей
- Поддерживает вложенные структуры

#### c_define_chain - Цепочка директив #define

Генерирует последовательность директив `#define` с правильным форматированием.

**Синтаксис:**
```
{{#c_define_chain}}
#define A 1
#define B 2
{{/c_define_chain}}
```

**Пример:**
```
{{#set VALUES=SUCCESS|ERROR|TIMEOUT}}
{{#c_define_chain}}
{{#for value in VALUES}}
#define STATUS_{{value}} {{value}}
{{/for}}
{{/c_define_chain}}
```

**Результат:**
```c
#define STATUS_SUCCESS SUCCESS
#define STATUS_ERROR ERROR
#define STATUS_TIMEOUT TIMEOUT
```

### Функции переменных

#### normalize_name

Преобразует значение в валидный C идентификатор, заменяя специальные символы.

**Синтаксис:** `{{VAR|normalize_name}}`

**Примеры:**
```
{{TYPE|normalize_name}}
```

- `dap_list_t*` → `dap_list_t_PTR`
- `char**` → `char_PTR_PTR`
- `const char*` → `const_char_PTR`

**Использование:**
```
{{#set TYPE=dap_list_t*}}
typedef {{TYPE}} {{TYPE|normalize_name}}_wrapper_t;
// Результат: typedef dap_list_t* dap_list_t_PTR_wrapper_t;
```

#### escape_name

Экранирует специальные символы в имени.

**Синтаксис:** `{{VAR|escape_name}}`

**Примеры:**
```
{{TYPE|escape_name}}
```

- `dap_list_t*` → `dap_list_t\*`
- `char**` → `char\*\*`

#### escape_char

Заменяет конкретный символ на указанную строку.

**Синтаксис:** `{{VAR|escape_char|char|replacement}}`

**Примеры:**
```
{{TYPE|escape_char|*|_PTR}}
```

- `dap_list_t*` → `dap_list_t_PTR`
- `char**` → `char_PTR_PTR`

#### sanitize_name

Удаляет недопустимые символы из имени, оставляя только буквы, цифры и подчёркивания.

**Синтаксис:** `{{VAR|sanitize_name}}`

**Примеры:**
```
{{NAME|sanitize_name}}
```

- `my-module` → `mymodule`
- `test@123` → `test123`
- `module.name` → `modulename`

**Использование:**
```
{{#set MODULE=dap-crypto}}
#ifndef {{MODULE|sanitize_name}}_MOCKS_H
#define {{MODULE|sanitize_name}}_MOCKS_H
// Результат: #ifndef dapcrypto_MOCKS_H
```

### Примеры использования

#### Пример 1: Генерация mock заголовка

```bash
# template.tpl
{{#c_ifdef ENABLE_MOCKS}}
#ifdef ENABLE_MOCKS
#ifndef {{MODULE|sanitize_name}}_MOCKS_H
#define {{MODULE|sanitize_name}}_MOCKS_H

#include "dap_mock.h"

{{#for func in FUNCTIONS}}
{{func|split|pipe}}
DAP_MOCK_DECLARE({{func|part|0}});
{{/for}}

#endif // {{MODULE|sanitize_name}}_MOCKS_H
{{#c_else}}
#else
// Mocks disabled
{{/c_ifdef}}
#endif
```

**Использование:**
```bash
replace_template_placeholders \
    template.tpl \
    output.h \
    --extensions-dir "../mocks/lib/dap_tpl" \
    "MODULE=dap-crypto" \
    "ENABLE_MOCKS=1" \
    "FUNCTIONS=init|int|void
cleanup|void|void
process|char*|const char* data"
```

#### Пример 2: Генерация типов с нормализацией

```bash
# types.tpl
{{#set TYPES=int|char*|dap_list_t*|void*}}
{{#for type in TYPES}}
typedef {{type}} {{type|normalize_name}}_wrapper_t;
{{/for}}
```

**Результат:**
```c
typedef int int_wrapper_t;
typedef char* char_PTR_wrapper_t;
typedef dap_list_t* dap_list_t_PTR_wrapper_t;
typedef void* void_PTR_wrapper_t;
```

### Тестирование

Все расширения мокинга протестированы в `dap-sdk/tests/integration/test-framework/`:

- `test_c_ifdef.sh` - Тесты для `c_ifdef`
- `test_return_type_macros.sh` - Тесты для генерации макросов возвращаемых типов
- `test_return_type_macros_dap_tpl.sh` - Тесты для чистого dap_tpl подхода

**Запуск тестов:**
```bash
cd dap-sdk/tests
./run.sh integration test-framework
```

Или через CTest:
```bash
cd dap-sdk/build
ctest -L test-framework --output-on-failure
```

### Структура расширений

```
mocks/lib/dap_tpl/
├── c_ifdef/
│   ├── definition.awk      # Регистрация конструкции
│   ├── tokenizer.awk       # Парсинг синтаксиса
│   ├── evaluator.awk       # Генерация C кода
│   └── branch_parser.awk  # Парсинг ветвей elif/else
├── c_for/
│   ├── definition.awk
│   ├── tokenizer.awk
│   └── evaluator.awk
├── c_struct/
│   ├── definition.awk
│   ├── tokenizer.awk
│   └── evaluator.awk
├── c_define_chain/
│   ├── definition.awk
│   ├── tokenizer.awk
│   └── evaluator.awk
└── variable_functions.awk  # Функции переменных (normalize_name, etc.)
```

