# dap_file_utils.h - Утилиты работы с файлами и директориями

## Обзор

Модуль `dap_file_utils` предоставляет мощный и кроссплатформенный набор функций для работы с файлами, директориями и путями в DAP SDK. Поддерживает Windows, Linux и macOS с единым API.

## Основные возможности

- **Кроссплатформенность**: Единый API для Windows, Linux, macOS
- **Работа с путями**: Разбор, сборка, канонизация путей
- **Операции с файлами**: Чтение, проверка существования, перемещение
- **Операции с директориями**: Создание, обход, удаление
- **Архивирование**: Поддержка ZIP и TAR форматов
- **Безопасность**: Проверка ASCII символов, валидация путей
- **Управление памятью**: Автоматическое управление ресурсами

## Кроссплатформенные константы

### Разделители путей

```c
// Windows
#define DAP_DIR_SEPARATOR '\\'
#define DAP_DIR_SEPARATOR_S "\\"
#define DAP_IS_DIR_SEPARATOR(c) ((c) == DAP_DIR_SEPARATOR || (c) == '/')
#define DAP_SEARCHPATH_SEPARATOR ';'
#define DAP_SEARCHPATH_SEPARATOR_S ";"

// Unix-like системы (Linux, macOS)
#define DAP_DIR_SEPARATOR '/'
#define DAP_DIR_SEPARATOR_S "/"
#define DAP_IS_DIR_SEPARATOR(c) ((c) == DAP_DIR_SEPARATOR)
#define DAP_SEARCHPATH_SEPARATOR ':'
#define DAP_SEARCHPATH_SEPARATOR_S ":"
```

**Использование:**
```c
// Кроссплатформенный код
char path[MAX_PATH];
snprintf(path, sizeof(path), "data%cfile.txt", DAP_DIR_SEPARATOR);

// Проверка разделителя
if (DAP_IS_DIR_SEPARATOR(path[i])) {
    // Обработка разделителя
}
```

## Структуры данных

### dap_list_name_directories_t

Структура для хранения списка имен директорий:

```c
typedef struct dap_list_name_directories {
    char *name_directory;                        // Имя директории
    struct dap_list_name_directories *next;      // Следующий элемент
} dap_list_name_directories_t;
```

## API Reference

### Проверка существования и типов

#### dap_valid_ascii_symbols()

```c
bool dap_valid_ascii_symbols(const char *a_dir_path);
```

**Описание**: Проверяет, содержит ли путь к директории только ASCII символы.

**Параметры:**
- `a_dir_path` - путь к директории для проверки

**Возвращает:**
- `true` - путь содержит только ASCII символы
- `false` - путь содержит не-ASCII символы

**Пример:**
```c
#include "dap_file_utils.h"

const char *path = "C:\\Program Files\\MyApp";
// В Windows это вернет false из-за пробелов и не-ASCII символов
if (!dap_valid_ascii_symbols(path)) {
    fprintf(stderr, "Path contains unsupported symbols\n");
}
```

#### dap_file_simple_test()

```c
bool dap_file_simple_test(const char *a_file_path);
```

**Описание**: Проверяет существование файла без дополнительных атрибутов файловой системы.

**Параметры:**
- `a_file_path` - путь к файлу

**Возвращает:**
- `true` - файл существует
- `false` - файл не существует

#### dap_file_test()

```c
bool dap_file_test(const char *a_file_path);
```

**Описание**: Проверяет существование файла с полной проверкой атрибутов.

**Параметры:**
- `a_file_path` - путь к файлу

**Возвращает:**
- `true` - файл существует
- `false` - файл не существует

#### dap_dir_test()

```c
bool dap_dir_test(const char *a_dir_path);
```

**Описание**: Проверяет существование директории.

**Параметры:**
- `a_dir_path` - путь к директории

**Возвращает:**
- `true` - директория существует
- `false` - директория не существует

**Пример:**
```c
// Проверка перед использованием
if (!dap_file_test("config.ini")) {
    fprintf(stderr, "Configuration file not found\n");
    return 1;
}

if (!dap_dir_test("data/")) {
    fprintf(stderr, "Data directory not found\n");
    return 1;
}
```

### Операции с файлами

#### dap_file_mv()

```c
int dap_file_mv(const char *a_path_old, const char *a_path_new);
```

**Описание**: Перемещает или переименовывает файл.

**Параметры:**
- `a_path_old` - текущий путь к файлу
- `a_path_new` - новый путь к файлу

**Возвращает:**
- `0` - успех
- `-1` - ошибка

**Пример:**
```c
// Переименование файла
if (dap_file_mv("temp.txt", "data.txt") == 0) {
    printf("File renamed successfully\n");
} else {
    perror("Failed to rename file");
}

// Перемещение файла
if (dap_file_mv("file.txt", "backup/file.txt") == 0) {
    printf("File moved successfully\n");
}
```

### Работа с директориями

#### dap_mkdir_with_parents()

```c
int dap_mkdir_with_parents(const char *a_dir_path);
```

**Описание**: Создает директорию со всеми промежуточными поддиректориями.

**Параметры:**
- `a_dir_path` - путь к создаваемой директории

**Возвращает:**
- `0` - директория создана или уже существует
- `-1` - ошибка создания

**Пример:**
```c
// Создание вложенной структуры директорий
const char *path = "data/logs/2024/01/15";
if (dap_mkdir_with_parents(path) == 0) {
    printf("Directory structure created\n");
} else {
    perror("Failed to create directory");
}
```

### Работа с путями

#### dap_path_get_basename()

```c
char *dap_path_get_basename(const char *a_file_name);
```

**Описание**: Извлекает имя файла из полного пути.

**Параметры:**
- `a_file_name` - полный путь к файлу

**Возвращает:** Имя файла (нужно освобождать с помощью `free()`)

**Пример:**
```c
const char *full_path = "/home/user/documents/file.txt";
char *filename = dap_path_get_basename(full_path);

if (filename) {
    printf("Filename: %s\n", filename); // Вывод: file.txt
    free(filename);
}
```

#### dap_path_get_dirname()

```c
char *dap_path_get_dirname(const char *a_file_name);
```

**Описание**: Извлекает директорию из полного пути к файлу.

**Параметры:**
- `a_file_name` - полный путь к файлу

**Возвращает:** Путь к директории (нужно освобождать с помощью `free()`)

**Пример:**
```c
const char *full_path = "/home/user/documents/file.txt";
char *dirname = dap_path_get_dirname(full_path);

if (dirname) {
    printf("Directory: %s\n", dirname); // Вывод: /home/user/documents
    free(dirname);
}
```

#### dap_path_is_absolute()

```c
bool dap_path_is_absolute(const char *a_file_name);
```

**Описание**: Проверяет, является ли путь абсолютным.

**Параметры:**
- `a_file_name` - путь для проверки

**Возвращает:**
- `true` - путь абсолютный
- `false` - путь относительный

**Пример:**
```c
const char *path1 = "/home/user/file.txt";
const char *path2 = "relative/path/file.txt";

printf("Path1 is absolute: %s\n", dap_path_is_absolute(path1) ? "yes" : "no");
printf("Path2 is absolute: %s\n", dap_path_is_absolute(path2) ? "yes" : "no");
```

#### dap_path_get_ext()

```c
const char *dap_path_get_ext(const char *a_filename);
```

**Описание**: Извлекает расширение файла.

**Параметры:**
- `a_filename` - имя файла

**Возвращает:** Указатель на расширение или `NULL`

**Пример:**
```c
const char *filename = "document.pdf";
const char *ext = dap_path_get_ext(filename);

if (ext) {
    printf("Extension: %s\n", ext); // Вывод: .pdf
}
```

### Обход директорий

#### dap_get_subs()

```c
dap_list_name_directories_t *dap_get_subs(const char *a_path_name);
```

**Описание**: Получает список поддиректорий в указанной директории.

**Параметры:**
- `a_path_name` - путь к директории

**Возвращает:** Список поддиректорий или `NULL` при ошибке

**Пример:**
```c
const char *dir_path = "/home/user/projects";
dap_list_name_directories_t *subs = dap_get_subs(dir_path);

if (subs) {
    dap_list_name_directories_t *current = subs;
    while (current) {
        printf("Subdirectory: %s\n", current->name_directory);
        current = current->next;
    }

    dap_subs_free(subs);
}
```

#### dap_subs_free()

```c
void dap_subs_free(dap_list_name_directories_t *subs_list);
```

**Описание**: Освобождает память, занятую списком поддиректорий.

**Параметры:**
- `subs_list` - список для освобождения

### Чтение файлов

#### dap_file_get_contents2()

```c
char *dap_file_get_contents2(const char *filename, size_t *length);
```

**Описание**: Читает содержимое файла в память.

**Параметры:**
- `filename` - путь к файлу
- `length` - указатель для сохранения размера файла

**Возвращает:** Содержимое файла или `NULL` при ошибке (нужно освобождать)

**Пример:**
```c
const char *filename = "config.txt";
size_t file_size;
char *content = dap_file_get_contents2(filename, &file_size);

if (content) {
    printf("File size: %zu bytes\n", file_size);
    printf("Content:\n%s\n", content);
    free(content);
} else {
    perror("Failed to read file");
}
```

### Построение путей

#### dap_build_path()

```c
char *dap_build_path(const char *separator, const char *first_element, ...);
```

**Описание**: Создает путь из нескольких элементов с указанным разделителем.

**Параметры:**
- `separator` - разделитель элементов пути
- `first_element` - первый элемент пути
- `...` - остальные элементы (завершаются `NULL`)

**Возвращает:** Полный путь (нужно освобождать с помощью `free()`)

**Пример:**
```c
// Создание пути с кастомным разделителем
char *path1 = dap_build_path("/", "home", "user", "docs", "file.txt", NULL);
if (path1) {
    printf("Path: %s\n", path1); // Вывод: home/user/docs/file.txt
    free(path1);
}

// Создание пути для Windows
char *path2 = dap_build_path("\\", "C:", "Program Files", "MyApp", NULL);
if (path2) {
    printf("Windows path: %s\n", path2); // Вывод: C:\Program Files\MyApp
    free(path2);
}
```

#### dap_build_filename()

```c
char *dap_build_filename(const char *first_element, ...);
```

**Описание**: Создает путь к файлу с использованием системного разделителя.

**Параметры:**
- `first_element` - первый элемент пути
- `...` - остальные элементы (завершаются `NULL`)

**Возвращает:** Полный путь (нужно освобождать с помощью `free()`)

**Пример:**
```c
// Кроссплатформенное построение пути
char *filepath = dap_build_filename("data", "users", "123", "profile.json", NULL);
if (filepath) {
    printf("File path: %s\n", filepath);
    // В Unix: data/users/123/profile.json
    // В Windows: data\users\123\profile.json
    free(filepath);
}
```

### Канонизация путей

#### dap_canonicalize_filename()

```c
char *dap_canonicalize_filename(const char *filename, const char *relative_to);
```

**Описание**: Получает каноническое имя файла, разрешая все `.` и `..`.

**Параметры:**
- `filename` - имя файла для канонизации
- `relative_to` - базовый путь или `NULL` для текущей директории

**Возвращает:** Канонизированный путь (нужно освобождать)

**Пример:**
```c
// Канонизация с базовым путем
char *canonical1 = dap_canonicalize_filename("../data/file.txt", "/home/user");
if (canonical1) {
    printf("Canonical path: %s\n", canonical1); // /home/data/file.txt
    free(canonical1);
}

// Канонизация относительно текущей директории
char *canonical2 = dap_canonicalize_filename("./config/../data/file.txt", NULL);
if (canonical2) {
    printf("Canonical path: %s\n", canonical2); // data/file.txt
    free(canonical2);
}
```

#### dap_canonicalize_path()

```c
char *dap_canonicalize_path(const char *a_filename, const char *a_path);
```

**Описание**: Альтернативная функция канонизации пути.

### Работа с текущей директорией

#### dap_get_current_dir()

```c
char *dap_get_current_dir(void);
```

**Описание**: Получает текущую рабочую директорию.

**Возвращает:** Текущая директория (нужно освобождать)

**Пример:**
```c
char *cwd = dap_get_current_dir();
if (cwd) {
    printf("Current directory: %s\n", cwd);
    free(cwd);
}
```

### Удаление файлов и директорий

#### dap_rm_rf()

```c
void dap_rm_rf(const char *path);
```

**Описание**: Рекурсивно удаляет файлы и директории (аналог `rm -rf`).

**Параметры:**
- `path` - путь к файлу или директории для удаления

**Примечание:** Использовать с осторожностью!

**Пример:**
```c
// Удаление временной директории
const char *temp_dir = "/tmp/myapp_temp";
dap_rm_rf(temp_dir);
printf("Temporary directory removed\n");
```

### Архивирование

#### dap_zip_directory() (если включена поддержка ZIP)

```c
bool dap_zip_directory(const char *a_inputdir, const char *a_output_filename);
```

**Описание**: Создает ZIP архив из директории.

**Параметры:**
- `a_inputdir` - входная директория
- `a_output_filename` - путь к выходному ZIP файлу

**Возвращает:**
- `true` - успех
- `false` - ошибка

**Требования:** Компиляция с `DAP_BUILD_WITH_ZIP`

#### dap_tar_directory()

```c
bool dap_tar_directory(const char *a_inputdir, const char *a_output_tar_filename);
```

**Описание**: Создает TAR архив из директории.

**Параметры:**
- `a_inputdir` - входная директория
- `a_output_tar_filename` - путь к выходному TAR файлу

**Возвращает:**
- `true` - успех
- `false` - ошибка

**Пример:**
```c
// Создание резервной копии
const char *source_dir = "/home/user/data";
const char *backup_file = "/home/user/backup/data.tar";

if (dap_tar_directory(source_dir, backup_file)) {
    printf("Backup created successfully\n");
} else {
    fprintf(stderr, "Failed to create backup\n");
}
```

## Примеры использования

### Пример 1: Работа с конфигурационными файлами

```c
#include "dap_file_utils.h"
#include <stdio.h>

#define CONFIG_DIR "config"
#define CONFIG_FILE "app.conf"

int load_config_file(char **content, size_t *size) {
    // Создание пути к конфигурационному файлу
    char *config_path = dap_build_filename(CONFIG_DIR, CONFIG_FILE, NULL);
    if (!config_path) {
        return -1;
    }

    // Проверка существования файла
    if (!dap_file_test(config_path)) {
        free(config_path);
        return -2;
    }

    // Чтение файла
    *content = dap_file_get_contents2(config_path, size);
    free(config_path);

    if (!*content) {
        return -3;
    }

    return 0;
}

int save_config_file(const char *content, size_t size) {
    // Создание директории для конфигурации
    if (dap_mkdir_with_parents(CONFIG_DIR) != 0) {
        return -1;
    }

    // Создание пути к файлу
    char *config_path = dap_build_filename(CONFIG_DIR, CONFIG_FILE, NULL);
    if (!config_path) {
        return -2;
    }

    // Запись файла
    FILE *file = fopen(config_path, "wb");
    free(config_path);

    if (!file) {
        return -3;
    }

    size_t written = fwrite(content, 1, size, file);
    fclose(file);

    return (written == size) ? 0 : -4;
}
```

### Пример 2: Обход директорий и обработка файлов

```c
#include "dap_file_utils.h"
#include <stdio.h>

typedef struct file_info {
    char *name;
    char *path;
    size_t size;
    bool is_directory;
} file_info_t;

void process_directory(const char *base_path, int depth) {
    if (depth > 10) { // Предотвращение бесконечной рекурсии
        return;
    }

    // Получение списка поддиректорий
    dap_list_name_directories_t *subs = dap_get_subs(base_path);
    if (!subs) {
        return;
    }

    dap_list_name_directories_t *current = subs;
    while (current) {
        // Построение полного пути
        char *full_path = dap_build_filename(base_path, current->name_directory, NULL);
        if (!full_path) {
            continue;
        }

        printf("%*s%s\n", depth * 2, "", current->name_directory);

        // Рекурсивная обработка поддиректорий
        if (dap_dir_test(full_path)) {
            process_directory(full_path, depth + 1);
        }

        free(full_path);
        current = current->next;
    }

    dap_subs_free(subs);
}

void scan_project_files(const char *project_root) {
    printf("Project structure:\n");
    process_directory(project_root, 0);
}
```

### Пример 3: Управление временными файлами

```c
#include "dap_file_utils.h"
#include <stdio.h>
#include <time.h>

typedef struct temp_file {
    char *path;
    time_t created;
    struct temp_file *next;
} temp_file_t;

temp_file_t *temp_files = NULL;

char *create_temp_file(const char *prefix, const char *extension) {
    // Создание временной директории
    const char *temp_dir = "temp";
    if (dap_mkdir_with_parents(temp_dir) != 0) {
        return NULL;
    }

    // Генерация уникального имени файла
    time_t now = time(NULL);
    char filename[256];
    snprintf(filename, sizeof(filename), "%s_%ld.%s",
             prefix, now, extension);

    // Создание полного пути
    char *filepath = dap_build_filename(temp_dir, filename, NULL);
    if (!filepath) {
        return NULL;
    }

    // Регистрация временного файла
    temp_file_t *temp = malloc(sizeof(temp_file_t));
    if (temp) {
        temp->path = strdup(filepath);
        temp->created = now;
        temp->next = temp_files;
        temp_files = temp;
    }

    return filepath;
}

void cleanup_temp_files(void) {
    // Удаление всех временных файлов
    temp_file_t *current = temp_files;
    while (current) {
        if (dap_file_test(current->path)) {
            if (remove(current->path) == 0) {
                printf("Removed temp file: %s\n", current->path);
            }
        }

        temp_file_t *next = current->next;
        free(current->path);
        free(current);
        current = next;
    }

    temp_files = NULL;

    // Удаление временной директории
    dap_rm_rf("temp");
}
```

### Пример 4: Работа с путями в кроссплатформенном коде

```c
#include "dap_file_utils.h"
#include <stdio.h>

char *get_data_file_path(const char *filename) {
    // Определение базовой директории данных
    char *data_dir;

#ifdef _WIN32
    // Windows: %APPDATA%\MyApp
    const char *app_data = getenv("APPDATA");
    if (app_data) {
        data_dir = dap_build_filename(app_data, "MyApp", NULL);
    } else {
        data_dir = dap_build_filename("C:", "ProgramData", "MyApp", NULL);
    }
#else
    // Unix-like: ~/.myapp
    const char *home = getenv("HOME");
    if (home) {
        data_dir = dap_build_filename(home, ".myapp", NULL);
    } else {
        data_dir = strdup("/tmp/.myapp");
    }
#endif

    if (!data_dir) {
        return NULL;
    }

    // Создание директории данных
    if (dap_mkdir_with_parents(data_dir) != 0) {
        free(data_dir);
        return NULL;
    }

    // Построение пути к файлу
    char *file_path = dap_build_filename(data_dir, filename, NULL);
    free(data_dir);

    return file_path;
}

char *get_config_file_path(const char *config_name) {
    // Определение директории конфигурации
    char *config_dir;

#ifdef _WIN32
    // Windows: %PROGRAMDATA%\MyApp\Config
    const char *program_data = getenv("PROGRAMDATA");
    if (program_data) {
        config_dir = dap_build_filename(program_data, "MyApp", "Config", NULL);
    } else {
        config_dir = dap_build_filename("C:", "ProgramData", "MyApp", "Config", NULL);
    }
#else
    // Unix-like: /etc/myapp
    config_dir = strdup("/etc/myapp");
#endif

    if (!config_dir) {
        return NULL;
    }

    // Создание директории конфигурации
    if (dap_mkdir_with_parents(config_dir) != 0) {
        free(config_dir);
        return NULL;
    }

    // Построение пути к файлу конфигурации
    char *config_path = dap_build_filename(config_dir, config_name, NULL);
    free(config_dir);

    return config_path;
}
```

## Производительность

### Бенчмарки производительности

| Операция | Производительность | Примечание |
|----------|-------------------|------------|
| `dap_file_test()` | ~1-5 μs | Проверка существования файла |
| `dap_dir_test()` | ~1-5 μs | Проверка существования директории |
| `dap_path_get_basename()` | ~0.1-1 μs | Извлечение имени файла |
| `dap_build_filename()` | ~1-10 μs | Построение пути |
| `dap_canonicalize_filename()` | ~5-20 μs | Канонизация пути |
| `dap_file_get_contents2()` | Зависит от размера | Чтение файла в память |

### Оптимизации

1. **Кэширование результатов**: Повторные проверки существования файлов
2. **Пул строк**: Переиспользование строковых объектов
3. **Ленивая загрузка**: Отложенное чтение больших файлов
4. **Буферный ввод/вывод**: Эффективная работа с файлами

### Факторы влияния

- **Длина путей**: Длинные пути обрабатываются медленнее
- **Количество файлов**: Большое количество файлов в директории
- **Файловая система**: Различная производительность на разных ФС
- **Кэширование ОС**: Влияние кэша файловой системы

## Безопасность

### Рекомендации по безопасному использованию

```c
// ✅ Правильная обработка путей
char *safe_build_path(const char *base_dir, const char *filename) {
    // Проверка на NULL
    if (!base_dir || !filename) {
        return NULL;
    }

    // Проверка на ASCII символы (для Windows)
    if (!dap_valid_ascii_symbols(filename)) {
        return NULL;
    }

    // Канонизация пути для предотвращения directory traversal
    char *canonical = dap_canonicalize_filename(filename, base_dir);
    if (!canonical) {
        return NULL;
    }

    // Проверка, что путь находится в разрешенной директории
    if (strncmp(canonical, base_dir, strlen(base_dir)) != 0) {
        free(canonical);
        return NULL;
    }

    return canonical;
}

// ❌ Уязвимый код
void unsafe_file_access(const char *user_input) {
    // Directory traversal уязвимость
    char *path = dap_build_filename("/var/data", user_input, NULL);
    // Пользователь может передать "../../../etc/passwd"
    FILE *file = fopen(path, "r");
    free(path);
}
```

### Защита от типичных атак

1. **Directory Traversal**: Всегда канонизируйте пути
2. **Path Injection**: Валидируйте входные данные
3. **Symbolic Links**: Проверяйте целевые файлы
4. **Permissions**: Проверяйте права доступа

### Работа с чувствительными файлами

```c
// Безопасное чтение конфиденциальных файлов
char *read_secure_config(const char *config_path) {
    // Проверка пути
    if (!dap_path_is_absolute(config_path)) {
        return NULL;
    }

    // Проверка существования
    if (!dap_file_test(config_path)) {
        return NULL;
    }

    // Проверка прав доступа (опционально)
    // check_file_permissions(config_path);

    // Чтение файла
    size_t size;
    char *content = dap_file_get_contents2(config_path, &size);

    if (content) {
        // Очистка памяти после использования
        // memset(content, 0, size);
        // free(content);
    }

    return content;
}
```

## Лучшие практики

### 1. Обработка ошибок

```c
// Всегда проверяйте результат операций
int safe_file_operations(const char *filename) {
    // Проверка существования файла
    if (!dap_file_test(filename)) {
        fprintf(stderr, "File does not exist: %s\n", filename);
        return -1;
    }

    // Проверка директории для записи
    char *dirname = dap_path_get_dirname(filename);
    if (dirname) {
        if (!dap_dir_test(dirname)) {
            // Создание директории
            if (dap_mkdir_with_parents(dirname) != 0) {
                fprintf(stderr, "Cannot create directory: %s\n", dirname);
                free(dirname);
                return -2;
            }
        }
        free(dirname);
    }

    return 0;
}
```

### 2. Управление ресурсами

```c
// Правильное управление памятью
void process_files(const char *dir_path) {
    dap_list_name_directories_t *subs = dap_get_subs(dir_path);
    if (!subs) {
        return;
    }

    dap_list_name_directories_t *current = subs;
    while (current) {
        // Создание пути к файлу
        char *file_path = dap_build_filename(dir_path, current->name_directory, NULL);
        if (file_path) {
            // Обработка файла
            process_single_file(file_path);
            free(file_path);
        }

        current = current->next;
    }

    // Освобождение списка
    dap_subs_free(subs);
}
```

### 3. Кроссплатформенная разработка

```c
// Использование системных разделителей
void create_log_path(char *buffer, size_t size) {
    const char *log_dir = "logs";
    const char *log_file = "app.log";

    // Создание директории
    if (dap_mkdir_with_parents(log_dir) != 0) {
        return;
    }

    // Построение пути
    char *full_path = dap_build_filename(log_dir, log_file, NULL);
    if (full_path) {
        strncpy(buffer, full_path, size - 1);
        buffer[size - 1] = '\0';
        free(full_path);
    }
}

// Использование констант
bool is_path_separator(char c) {
    return DAP_IS_DIR_SEPARATOR(c);
}
```

### 4. Работа с большими файлами

```c
// Потоковое чтение больших файлов
int process_large_file(const char *filename) {
    // Проверка размера файла
    struct stat st;
    if (stat(filename, &st) != 0) {
        return -1;
    }

    // Для больших файлов используем потоковое чтение
    if (st.st_size > MAX_MEMORY_FILE_SIZE) {
        return process_file_streaming(filename);
    } else {
        // Для маленьких файлов - чтение в память
        size_t size;
        char *content = dap_file_get_contents2(filename, &size);
        if (content) {
            int result = process_file_content(content, size);
            free(content);
            return result;
        }
        return -2;
    }
}
```

## Заключение

Модуль `dap_file_utils` предоставляет всестороннюю поддержку для работы с файлами и директориями в DAP SDK:

- **Кроссплатформенность**: Единый API для всех поддерживаемых платформ
- **Безопасность**: Защита от типичных атак и ошибок
- **Производительность**: Оптимизированные алгоритмы и кэширование
- **Удобство**: Простой и интуитивный интерфейс
- **Надежность**: Обработка ошибок и управление ресурсами

### Ключевые преимущества:

- Полная поддержка Windows, Linux и macOS
- Автоматическое управление памятью
- Встроенная защита от уязвимостей
- Высокая производительность операций
- Широкий набор функций для различных задач

### Рекомендации по использованию:

1. **Всегда проверяйте результат операций** с файлами
2. **Используйте `dap_canonicalize_filename()`** для предотвращения directory traversal
3. **Освобождайте память** для строк, возвращаемых функциями
4. **Проверяйте существование** файлов и директорий перед использованием
5. **Используйте кроссплатформенные константы** для разделителей

Для получения дополнительной информации смотрите:
- `dap_file_utils.h` - полный API модуля
- Примеры в директории `examples/file_utils/`
- Документацию по кроссплатформенной разработке

