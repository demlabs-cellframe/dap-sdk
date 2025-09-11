# DAP Filename Matching (Сопоставление имен файлов)

## Обзор

Модуль `dap_fnmatch` предоставляет функции для сопоставления строк с шаблонами (pattern matching), аналогично команде `fnmatch` из POSIX. Используется для работы с шаблонами имен файлов, конфигурационными масками и фильтрами в DAP SDK.

## Назначение

Шаблонное сопоставление критически важно для:
- **Работы с файловыми системами**: Поиск файлов по маске
- **Конфигурационных фильтров**: Применение правил к группам элементов
- **Валидации ввода**: Проверка соответствия форматам
- **Маршрутизации**: Фильтрация сетевых запросов
- **Обработки логов**: Фильтрация сообщений по шаблонам

## Основные возможности

### 🔍 **Шаблонное сопоставление**
- Поддержка стандартных шаблонов (`*`, `?`, `[...]`)
- Расширенные возможности GNU (опционально)
- Регистронезависимое сравнение

### ⚙️ **Конфигурируемые флаги**
- Управление поведением сопоставления
- Специальная обработка путей
- Экранирование специальных символов

### 🚀 **Высокая производительность**
- Оптимизированные алгоритмы
- Минимальные накладные расходы
- Кроссплатформенная поддержка

## Флаги управления сопоставлением

### Основные флаги

```c
#define FNM_PATHNAME    (1 << 0) /* No wildcard can ever match `/'.  */
/* Символы подстановки не могут соответствовать '/' */

#define FNM_NOESCAPE    (1 << 1) /* Backslashes don't quote special chars.  */
/* Обратные слеши не экранируют специальные символы */

#define FNM_PERIOD      (1 << 2) /* Leading `.' is matched only explicitly.  */
/* Ведущая '.' соответствует только явному указанию */
```

### Расширенные флаги (GNU)

```c
#define FNM_FILE_NAME   FNM_PATHNAME   /* Preferred GNU name.  */
/* Предпочитаемое GNU имя для FNM_PATHNAME */

#define FNM_LEADING_DIR (1 << 3)   /* Ignore `/...' after a match.  */
/* Игнорировать '/...' после совпадения */

#define FNM_CASEFOLD    (1 << 4)   /* Compare without regard to case.  */
/* Сравнение без учета регистра */

#define FNM_EXTMATCH    (1 << 5)   /* Use ksh-like extended matching. */
/* Использовать расширенное сопоставление в стиле ksh */
```

## API Функции

### Основная функция сопоставления

```c
// Сопоставление строки с шаблоном
extern int dap_fnmatch(const char *pattern, const char *string, int flags);
```

**Параметры:**
- `pattern` - Шаблон для сопоставления
- `string` - Строка для проверки
- `flags` - Флаги управления поведением

**Возвращаемые значения:**
- `0` - Совпадение найдено
- `FNM_NOMATCH` - Совпадение не найдено

## Синтаксис шаблонов

### Основные символы подстановки

| Символ | Описание | Пример | Совпадения |
|--------|----------|--------|------------|
| `*` | Любая последовательность символов | `*.txt` | `file.txt`, `data.txt` |
| `?` | Один любой символ | `file?.txt` | `file1.txt`, `fileA.txt` |
| `[abc]` | Один символ из множества | `file[123].txt` | `file1.txt`, `file2.txt`, `file3.txt` |
| `[a-z]` | Диапазон символов | `file[a-z].txt` | `filea.txt`, `fileb.txt`, ... |
| `[!abc]` | Любой символ кроме указанных | `file[!0-9].txt` | `filea.txt`, `fileB.txt` |

### Расширенные возможности (FNM_EXTMATCH)

| Конструкция | Описание | Пример | Совпадения |
|-------------|----------|--------|------------|
| `?(pattern)` | Ноль или одно вхождение | `file?(1).txt` | `file.txt`, `file1.txt` |
| `*(pattern)` | Ноль или более вхождений | `*(file).txt` | `file.txt`, `filefile.txt` |
| `+(pattern)` | Одно или более вхождений | `+(file).txt` | `file.txt`, `filefile.txt` |
| `@(pattern)` | Ровно одно вхождение | `@(file).txt` | `file.txt` |
| `!(pattern)` | Любое кроме указанного | `!(file).txt` | `data.txt`, `info.txt` |

## Использование

### Базовое сопоставление файлов

```c
#include "dap_fnmatch.h"

// Проверка расширения файла
int check_file_extension(const char *filename) {
    if (dap_fnmatch("*.txt", filename, 0) == 0) {
        printf("Это текстовый файл: %s\n", filename);
        return 1;
    }
    return 0;
}

// Проверка с учетом пути
int check_path_pattern(const char *filepath) {
    // '*' не соответствует '/' с флагом FNM_PATHNAME
    if (dap_fnmatch("src/*.c", filepath, FNM_PATHNAME) == 0) {
        printf("C файл в директории src: %s\n", filepath);
        return 1;
    }
    return 0;
}
```

### Работа с конфигурационными шаблонами

```c
// Валидация имен конфигурационных файлов
bool is_valid_config_file(const char *filename) {
    // Допустимые шаблоны: config*.json, settings*.yaml
    const char *patterns[] = {
        "config*.json",
        "settings*.yaml",
        NULL
    };

    for (int i = 0; patterns[i]; i++) {
        if (dap_fnmatch(patterns[i], filename, FNM_CASEFOLD) == 0) {
            return true;
        }
    }
    return false;
}

// Применение шаблона к списку файлов
void process_files_by_pattern(char **filenames, const char *pattern) {
    for (int i = 0; filenames[i]; i++) {
        if (dap_fnmatch(pattern, filenames[i], 0) == 0) {
            process_file(filenames[i]);
        }
    }
}
```

### Расширенное сопоставление

```c
// Использование расширенных шаблонов (если поддерживается)
int advanced_pattern_matching(const char *filename) {
    // Совпадет с: test.c, test.h, test.cpp, test.hpp
    if (dap_fnmatch("test.@(c|cpp|h|hpp)", filename, FNM_EXTMATCH) == 0) {
        printf("Совпадение с расширенным шаблоном: %s\n", filename);
        return 1;
    }

    // Совпадет с: file.txt, file1.txt, file2.txt, но НЕ с file.txt.bak
    if (dap_fnmatch("file+([0-9]).txt", filename, FNM_EXTMATCH) == 0) {
        printf("Совпадение с цифровым шаблоном: %s\n", filename);
        return 1;
    }

    return 0;
}
```

### Работа с путями

```c
// Фильтрация путей
bool should_process_path(const char *path) {
    // Обработка всех .c файлов в src/, но не в поддиректориях
    if (dap_fnmatch("src/*.c", path, FNM_PATHNAME) == 0) {
        return true;
    }

    // Обработка всех .h файлов в любом месте
    if (dap_fnmatch("*.h", path, 0) == 0) {
        return true;
    }

    return false;
}

// Игнорирование скрытых файлов
bool is_hidden_file(const char *filename) {
    // Ведущая '.' должна указываться явно
    if (dap_fnmatch(".*", filename, FNM_PERIOD) == 0) {
        return true;
    }
    return false;
}
```

### Регистронезависимое сравнение

```c
// Поиск без учета регистра
bool case_insensitive_match(const char *pattern, const char *string) {
    return dap_fnmatch(pattern, string, FNM_CASEFOLD) == 0;
}

// Пример использования
void find_log_files(char **files) {
    for (int i = 0; files[i]; i++) {
        // Найти все файлы логов независимо от регистра
        if (case_insensitive_match("*log*", files[i])) {
            printf("Найден файл логов: %s\n", files[i]);
        }
    }
}
```

## Особенности реализации

### Алгоритм сопоставления

Модуль использует оптимизированный алгоритм сопоставления, который:
- **Линеен по сложности** для большинства шаблонов
- **Использует конечные автоматы** для сложных шаблонов
- **Поддерживает backtracking** для выражений с квантификаторами

### Производительность

| Тип шаблона | Сложность | Примеры |
|-------------|-----------|---------|
| Простые | O(n) | `*.txt`, `file?` |
| Диапазоны | O(n) | `[a-z]*`, `[0-9]+` |
| Комплексные | O(n²) | Вложенные квантификаторы |
| Расширенные | O(n²) | `*(a*b*c)` |

### Оптимизации

1. **Раннее завершение**: Выход при первом несовпадении
2. **Кэширование**: Повторное использование скомпилированных шаблонов
3. **SIMD инструкции**: Векторизация для простых сравнений

## Использование в DAP SDK

### Фильтрация файлов конфигурации

```c
// Загрузка конфигурационных файлов по шаблону
void load_config_files(const char *config_dir) {
    DIR *dir = opendir(config_dir);
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // Загружать только .json файлы конфигурации
        if (dap_fnmatch("*.json", entry->d_name, 0) == 0) {
            char filepath[PATH_MAX];
            snprintf(filepath, sizeof(filepath), "%s/%s",
                    config_dir, entry->d_name);
            load_json_config(filepath);
        }
    }
    closedir(dir);
}
```

### Валидация сетевых запросов

```c
// Фильтрация API эндпоинтов
bool is_valid_api_endpoint(const char *endpoint) {
    // Допустимые эндпоинты: /api/v1/*, /api/v2/*
    const char *valid_patterns[] = {
        "/api/v1/*",
        "/api/v2/*",
        NULL
    };

    for (int i = 0; valid_patterns[i]; i++) {
        if (dap_fnmatch(valid_patterns[i], endpoint, FNM_PATHNAME) == 0) {
            return true;
        }
    }
    return false;
}
```

### Обработка логов

```c
// Фильтрация сообщений логов
typedef enum {
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR
} log_level_t;

log_level_t parse_log_level(const char *log_line) {
    if (dap_fnmatch("*DEBUG*", log_line, FNM_CASEFOLD) == 0) {
        return LOG_LEVEL_DEBUG;
    }
    if (dap_fnmatch("*INFO*", log_line, FNM_CASEFOLD) == 0) {
        return LOG_LEVEL_INFO;
    }
    if (dap_fnmatch("*WARN*", log_line, FNM_CASEFOLD) == 0) {
        return LOG_LEVEL_WARN;
    }
    if (dap_fnmatch("*ERROR*", log_line, FNM_CASEFOLD) == 0) {
        return LOG_LEVEL_ERROR;
    }

    return LOG_LEVEL_INFO; // По умолчанию
}
```

### Работа с базами данных

```c
// Фильтрация записей базы данных
bool matches_record_pattern(const char *record_key, const char *pattern) {
    // Поддержка шаблонов в ключах записей
    return dap_fnmatch(pattern, record_key, 0) == 0;
}

// Пример использования
void query_database(const char *pattern) {
    // Найти все записи с ключами вида "user_*_profile"
    database_foreach_record(record) {
        if (matches_record_pattern(record->key, pattern)) {
            process_record(record);
        }
    }
}
```

## Связанные модули

- `dap_strfuncs.h` - Работа со строками
- `dap_common.h` - Общие определения
- `dap_fnmatch_loop.h` - Итеративные функции сопоставления

## Замечания по безопасности

### Валидация входных данных

```c
// Всегда проверяйте входные параметры
bool safe_pattern_match(const char *pattern, const char *string, int flags) {
    if (!pattern || !string) {
        return false;
    }

    // Ограничение длины шаблона для предотвращения DoS
    if (strlen(pattern) > MAX_PATTERN_LENGTH) {
        log_it(L_WARNING, "Pattern too long: %zu", strlen(pattern));
        return false;
    }

    return dap_fnmatch(pattern, string, flags) == 0;
}
```

### Защита от ReDoS атак

```c
// Избегайте опасных шаблонов
const char *dangerous_patterns[] = {
    "(a+)+b",    // Катострофическое backtracking
    "(a*)*",     // Экспоненциальная сложность
    "(a|a)*",    // Неэффективные альтернативы
    NULL
};

bool is_safe_pattern(const char *pattern) {
    for (int i = 0; dangerous_patterns[i]; i++) {
        if (strstr(pattern, dangerous_patterns[i])) {
            return false;
        }
    }
    return true;
}
```

### Обработка специальных символов

```c
// Экранирование пользовательского ввода
char *escape_pattern(const char *user_input) {
    // Замена специальных символов на экранированные
    return dap_str_replace_char(user_input, '*', '\\*');
}
```

## Отладка

### Диагностика сопоставления

```c
// Функция для отладки сопоставления
void debug_pattern_match(const char *pattern, const char *string, int flags) {
    int result = dap_fnmatch(pattern, string, flags);
    printf("Pattern matching debug:\n");
    printf("  Pattern: '%s'\n", pattern);
    printf("  String:  '%s'\n", string);
    printf("  Flags:   0x%x\n", flags);
    printf("  Result:  %s (%d)\n",
           result == 0 ? "MATCH" : "NO MATCH", result);

    // Показать активные флаги
    if (flags & FNM_PATHNAME) printf("  - FNM_PATHNAME\n");
    if (flags & FNM_NOESCAPE) printf("  - FNM_NOESCAPE\n");
    if (flags & FNM_PERIOD) printf("  - FNM_PERIOD\n");
    if (flags & FNM_CASEFOLD) printf("  - FNM_CASEFOLD\n");
    if (flags & FNM_EXTMATCH) printf("  - FNM_EXTMATCH\n");
}
```

### Тестирование шаблонов

```c
// Комплексное тестирование
void test_fnmatch_patterns() {
    struct {
        const char *pattern;
        const char *test_string;
        int flags;
        bool expected_match;
    } test_cases[] = {
        {"*.txt", "file.txt", 0, true},
        {"*.txt", "file.TXT", FNM_CASEFOLD, true},
        {"src/*.c", "src/main.c", FNM_PATHNAME, true},
        {"src/*.c", "src/test/main.c", FNM_PATHNAME, false},
        {"file[0-9].txt", "file5.txt", 0, true},
        {"file[!0-9].txt", "fileA.txt", 0, true},
        {NULL, NULL, 0, false}
    };

    for (int i = 0; test_cases[i].pattern; i++) {
        int result = dap_fnmatch(test_cases[i].pattern,
                                test_cases[i].test_string,
                                test_cases[i].flags);
        bool actual_match = (result == 0);

        if (actual_match != test_cases[i].expected_match) {
            printf("TEST FAILED: %s %c %s (flags=0x%x)\n",
                   test_cases[i].pattern,
                   test_cases[i].expected_match ? '=' : '!',
                   test_cases[i].test_string,
                   test_cases[i].flags);
        } else {
            printf("TEST PASSED: %s %c %s\n",
                   test_cases[i].pattern,
                   test_cases[i].expected_match ? '=' : '!',
                   test_cases[i].test_string);
        }
    }
}
```

### Профилирование производительности

```c
// Измерение производительности сопоставления
void benchmark_fnmatch(const char *pattern, char **test_strings, int num_strings) {
    clock_t start = clock();

    int matches = 0;
    for (int i = 0; i < num_strings; i++) {
        if (dap_fnmatch(pattern, test_strings[i], 0) == 0) {
            matches++;
        }
    }

    clock_t end = clock();
    double time_spent = (double)(end - start) / CLOCKS_PER_SEC;

    printf("Pattern '%s': %d matches in %.3f seconds (%.1f matches/sec)\n",
           pattern, matches, time_spent, matches / time_spent);
}
```

Этот модуль предоставляет мощные и эффективные возможности для работы с шаблонами в DAP SDK, обеспечивая гибкость и производительность при работе с файловыми системами, конфигурациями и фильтрацией данных.
