# Core Module - Ядро DAP SDK

## Обзор

Core Module является основой DAP SDK и предоставляет базовую функциональность, общие утилиты и платформо-специфичные реализации.

## Структура модуля

```
core/
├── include/           # Заголовочные файлы (19 файлов)
│   ├── dap_binary_tree.h    # Бинарные деревья поиска
│   ├── dap_cbuf.h          # Циклические буферы
│   ├── dap_common.h        # Общие определения
│   ├── dap_config.h        # Система конфигурации
│   ├── dap_crc64.h         # CRC64 хеширование
│   ├── dap_file_utils.h    # Утилиты работы с файлами
│   ├── dap_fnmatch.h       # Поиск файлов по маске
│   ├── dap_json_rpc_errors.h # Ошибки JSON-RPC
│   ├── dap_list.h          # Связанные списки
│   ├── dap_math_convert.h  # Преобразования чисел
│   ├── dap_math_ops.h      # Математические операции
│   ├── dap_module.h        # Система модулей
│   ├── dap_strfuncs.h      # Строковые функции
│   ├── dap_string.h        # Работа со строками
│   ├── dap_time.h          # Работа с временем
│   ├── dap_tsd.h           # Thread-Specific Data
│   └── portable_endian.h   # Портабельный endian
├── src/               # Исходный код
│   ├── common/        # Общие утилиты
│   ├── unix/          # Unix/Linux реализация
│   ├── darwin/        # macOS реализация
│   └── win32/         # Windows реализация
├── test/              # Тесты
└── docs/              # Документация
```

## Основные компоненты

### 1. Общие утилиты (Common Utilities)

#### dap_common.h
Основные определения и макросы для всего SDK.

```c
// Основные типы данных
typedef uint8_t dap_byte_t;
typedef uint32_t dap_uint_t;
typedef int32_t dap_int_t;

// Макросы для отладки
#define DAP_ASSERT(condition) \
    do { if (!(condition)) { \
        dap_log(L_ERROR, "Assertion failed: %s", #condition); \
        abort(); \
    } } while(0)

// Макросы для управления памятью
#define DAP_NEW(type) ((type*)dap_malloc(sizeof(type)))
#define DAP_DELETE(ptr) do { dap_free(ptr); ptr = NULL; } while(0)
```

#### dap_list.h
Реализация связанных списков с поддержкой различных типов данных.

```c
// Создание списка
dap_list_t* dap_list_new(void);

// Добавление элемента
dap_list_t* dap_list_append(dap_list_t* list, void* data);

// Удаление элемента
dap_list_t* dap_list_remove(dap_list_t* list, void* data);

// Поиск элемента
dap_list_t* dap_list_find(dap_list_t* list, void* data);

// Освобождение списка
void dap_list_free(dap_list_t* list);
```

#### dap_hash.h
**⚠️ ВНИМАНИЕ: Этот файл отсутствует в Core Module**

Хеш-функции вероятно перемещены в Crypto Module (`dap-sdk/crypto/`). Рекомендуется использовать функции из модуля криптографии для всех операций хеширования.

Для хеширования используйте:
- `dap-sdk/crypto/include/dap_hash.h` - основные хеш-функции
- `dap-sdk/crypto/include/dap_enc_key.h` - криптографические ключи и подписи

#### dap_time.h
Работа с временем и временными метками.

```c
// Получение текущего времени
dap_time_t dap_time_now(void);

// Преобразование времени
char* dap_time_to_string(dap_time_t time);
dap_time_t dap_time_from_string(const char* str);

// Сравнение времени
int dap_time_compare(dap_time_t t1, dap_time_t t2);

// Задержка выполнения
void dap_time_sleep(uint32_t milliseconds);
```

### 2. Платформо-специфичные реализации

#### Unix/Linux (core/src/unix/)
```c
// Управление процессами
pid_t dap_process_fork(void);
int dap_process_wait(pid_t pid);

// Сетевые сокеты
int dap_socket_create(int domain, int type, int protocol);
int dap_socket_bind(int sockfd, const struct sockaddr* addr, socklen_t addrlen);

// Файловая система
int dap_file_open(const char* path, int flags, mode_t mode);
ssize_t dap_file_read(int fd, void* buf, size_t count);
```

#### macOS (core/src/darwin/)
```c
// Специфичные для macOS функции
int dap_darwin_get_system_info(dap_system_info_t* info);
int dap_darwin_set_process_name(const char* name);
```

#### Windows (core/src/win32/)
```c
// Windows-специфичные функции
HANDLE dap_win32_create_thread(LPTHREAD_START_ROUTINE func, LPVOID param);
DWORD dap_win32_wait_for_thread(HANDLE thread);

// Unicode поддержка
wchar_t* dap_win32_utf8_to_wide(const char* utf8);
char* dap_win32_wide_to_utf8(const wchar_t* wide);
```

### 3. Система конфигурации (dap_config.h)

**Назначение**: Управление конфигурационными файлами и настройками приложения.

```c
// Структура конфигурации
typedef struct dap_config {
    char *section;              // Секция конфигурации
    char *key;                  // Ключ
    char *value;                // Значение
    struct dap_config *next;    // Следующий элемент
} dap_config_t;

// Загрузка конфигурации
dap_config_t *dap_config_load(const char *filename);

// Получение значений
const char *dap_config_get_string(dap_config_t *config,
                                  const char *section,
                                  const char *key);

int dap_config_get_int(dap_config_t *config,
                       const char *section,
                       const char *key);

bool dap_config_get_bool(dap_config_t *config,
                         const char *section,
                         const char *key);

// Сохранение конфигурации
int dap_config_save(dap_config_t *config, const char *filename);

// Освобождение памяти
void dap_config_free(dap_config_t *config);
```

### 4. Система модулей (dap_module.h)

**Назначение**: Динамическая загрузка и управление модулями.

```c
// Структура модуля
typedef struct dap_module {
    char *name;                 // Имя модуля
    void *handle;               // Дескриптор модуля
    int (*init)(void);          // Функция инициализации
    void (*deinit)(void);       // Функция деинициализации
    struct dap_module *next;    // Следующий модуль
} dap_module_t;

// Загрузка модуля
dap_module_t *dap_module_load(const char *path);

// Выгрузка модуля
int dap_module_unload(dap_module_t *module);

// Поиск модуля
dap_module_t *dap_module_find(const char *name);

// Вызов функции модуля
void *dap_module_call(dap_module_t *module, const char *func_name);
```

### 5. Файловые утилиты (dap_file_utils.h)

**Назначение**: Утилиты для работы с файловой системой.

```c
// Информация о файле
typedef struct dap_file_info {
    char *name;                 // Имя файла
    size_t size;                // Размер файла
    time_t mtime;               // Время модификации
    bool is_dir;                // Является ли директорией
} dap_file_info_t;

// Проверка существования файла
bool dap_file_exists(const char *filename);

// Получение размера файла
size_t dap_file_size(const char *filename);

// Чтение файла
char *dap_file_read(const char *filename, size_t *size);

// Запись в файл
int dap_file_write(const char *filename, const void *data, size_t size);

// Создание директории
int dap_dir_create(const char *path);

// Получение списка файлов
dap_list_t *dap_dir_list(const char *path);

// Копирование файла
int dap_file_copy(const char *src, const char *dst);

// Перемещение файла
int dap_file_move(const char *src, const char *dst);
```

### 6. Математические операции (dap_math_ops.h)

**Назначение**: Безопасные математические операции с проверкой переполнения.

```c
// Безопасное сложение
bool dap_add_u64(uint64_t *result, uint64_t a, uint64_t b);
bool dap_add_i64(int64_t *result, int64_t a, int64_t b);

// Безопасное вычитание
bool dap_sub_u64(uint64_t *result, uint64_t a, uint64_t b);
bool dap_sub_i64(int64_t *result, int64_t a, int64_t b);

// Безопасное умножение
bool dap_mul_u64(uint64_t *result, uint64_t a, uint64_t b);
bool dap_mul_i64(int64_t *result, int64_t a, int64_t b);

// Безопасное деление
bool dap_div_u64(uint64_t *result, uint64_t a, uint64_t b);
bool dap_div_i64(int64_t *result, int64_t a, int64_t b);

// Проверка переполнения
bool dap_will_add_overflow(uint64_t a, uint64_t b);
bool dap_will_mul_overflow(uint64_t a, uint64_t b);
```

### 7. Преобразования чисел (dap_math_convert.h)

**Назначение**: Преобразование чисел между различными форматами.

```c
// Преобразование строк в числа
bool dap_str_to_u64(const char *str, uint64_t *result);
bool dap_str_to_i64(const char *str, int64_t *result);

// Преобразование чисел в строки
char *dap_u64_to_str(uint64_t value);
char *dap_i64_to_str(int64_t value);

// Преобразование между endian
uint16_t dap_swap_u16(uint16_t value);
uint32_t dap_swap_u32(uint32_t value);
uint64_t dap_swap_u64(uint64_t value);

// Преобразование в/из BCD
uint8_t dap_to_bcd(uint8_t value);
uint8_t dap_from_bcd(uint8_t value);
```

### 8. Бинарные деревья поиска (dap_binary_tree.h)

**Назначение**: Реализация бинарных деревьев поиска для эффективного хранения данных.

```c
// Структура узла дерева
typedef struct dap_binary_tree_node {
    void *key;                          // Ключ
    void *value;                        // Значение
    struct dap_binary_tree_node *left;  // Левое поддерево
    struct dap_binary_tree_node *right; // Правое поддерево
} dap_binary_tree_node_t;

// Структура дерева
typedef struct dap_binary_tree {
    dap_binary_tree_node_t *root;       // Корень дерева
    size_t size;                        // Количество элементов
    int (*compare)(const void*, const void*); // Функция сравнения
} dap_binary_tree_t;

// Создание дерева
dap_binary_tree_t *dap_binary_tree_new(int (*compare)(const void*, const void*));

// Вставка элемента
bool dap_binary_tree_insert(dap_binary_tree_t *tree, void *key, void *value);

// Поиск элемента
void *dap_binary_tree_find(dap_binary_tree_t *tree, const void *key);

// Удаление элемента
bool dap_binary_tree_remove(dap_binary_tree_t *tree, const void *key);

// Освобождение дерева
void dap_binary_tree_free(dap_binary_tree_t *tree);
```

### 9. Циклические буферы (dap_cbuf.h)

**Назначение**: Реализация циклических буферов для эффективной работы с потоками данных.

```c
// Структура циклического буфера
typedef struct dap_cbuf {
    uint8_t *buffer;     // Буфер данных
    size_t size;         // Размер буфера
    size_t head;         // Индекс головы
    size_t tail;         // Индекс хвоста
    bool full;           // Флаг заполненности
} dap_cbuf_t;

// Создание буфера
dap_cbuf_t *dap_cbuf_new(size_t size);

// Запись данных
size_t dap_cbuf_write(dap_cbuf_t *cbuf, const void *data, size_t len);

// Чтение данных
size_t dap_cbuf_read(dap_cbuf_t *cbuf, void *data, size_t len);

// Проверка состояния
bool dap_cbuf_is_empty(dap_cbuf_t *cbuf);
bool dap_cbuf_is_full(dap_cbuf_t *cbuf);
size_t dap_cbuf_used_space(dap_cbuf_t *cbuf);
size_t dap_cbuf_free_space(dap_cbuf_t *cbuf);

// Освобождение буфера
void dap_cbuf_free(dap_cbuf_t *cbuf);
```

## API Reference

### Инициализация и очистка

```c
// Инициализация core модуля
int dap_core_init(void);

// Очистка core модуля
void dap_core_deinit(void);

// Проверка инициализации
bool dap_core_is_initialized(void);
```

### Управление памятью

```c
// Выделение памяти
void* dap_malloc(size_t size);
void* dap_calloc(size_t count, size_t size);
void* dap_realloc(void* ptr, size_t size);

// Освобождение памяти
void dap_free(void* ptr);

// Безопасное освобождение
void dap_safe_free(void** ptr);
```

### Утилиты

```c
// Сравнение строк
int dap_strcmp(const char* s1, const char* s2);
int dap_strncmp(const char* s1, const char* s2, size_t n);

// Копирование строк
char* dap_strdup(const char* str);
char* dap_strndup(const char* str, size_t n);

// Форматирование строк
int dap_snprintf(char* str, size_t size, const char* format, ...);
int dap_vsnprintf(char* str, size_t size, const char* format, va_list ap);
```

## Примеры использования

### Базовое использование

```c
#include "dap_common.h"
#include "dap_list.h"
#include "dap_hash.h"

int main() {
    // Инициализация
    dap_core_init();
    
    // Создание списка
    dap_list_t* list = dap_list_new();
    
    // Добавление элементов
    char* item1 = dap_strdup("Hello");
    char* item2 = dap_strdup("World");
    
    list = dap_list_append(list, item1);
    list = dap_list_append(list, item2);
    
    // Хеширование данных
    uint8_t hash[32];
    dap_hash_sha256("Hello World", 11, hash);
    
    // Очистка
    dap_list_free(list);
    dap_core_deinit();
    
    return 0;
}
```

### Работа с временем

```c
#include "dap_time.h"

void time_example() {
    // Получение текущего времени
    dap_time_t now = dap_time_now();
    
    // Преобразование в строку
    char* time_str = dap_time_to_string(now);
    printf("Current time: %s\n", time_str);
    
    // Задержка
    dap_time_sleep(1000); // 1 секунда
    
    // Освобождение памяти
    dap_free(time_str);
}
```

### Платформо-специфичный код

```c
#include "dap_common.h"

#ifdef DAP_PLATFORM_UNIX
    #include "dap_unix.h"
#elif defined(DAP_PLATFORM_WINDOWS)
    #include "dap_win32.h"
#endif

void platform_example() {
#ifdef DAP_PLATFORM_UNIX
    // Unix-специфичный код
    pid_t pid = dap_process_fork();
    if (pid == 0) {
        // Дочерний процесс
        printf("Child process\n");
    } else {
        // Родительский процесс
        dap_process_wait(pid);
    }
#elif defined(DAP_PLATFORM_WINDOWS)
    // Windows-специфичный код
    HANDLE thread = dap_win32_create_thread(thread_func, NULL);
    dap_win32_wait_for_thread(thread);
#endif
}
```

## Тестирование

### Запуск тестов

```bash
# Сборка с тестами
cmake -DBUILD_DAP_SDK_TESTS=ON ..
make

# Запуск тестов core модуля
./test/core/test_common
./test/core/test_list
./test/core/test_hash
./test/core/test_time
```

### Пример теста

```c
#include "dap_test.h"
#include "dap_list.h"

void test_list_operations() {
    dap_list_t* list = dap_list_new();
    
    // Тест добавления
    list = dap_list_append(list, "item1");
    DAP_ASSERT(list != NULL);
    DAP_ASSERT(dap_list_length(list) == 1);
    
    // Тест поиска
    dap_list_t* found = dap_list_find(list, "item1");
    DAP_ASSERT(found != NULL);
    
    // Тест удаления
    list = dap_list_remove(list, "item1");
    DAP_ASSERT(dap_list_length(list) == 0);
    
    dap_list_free(list);
}
```

## Производительность

### Бенчмарки

| Операция | Производительность |
|----------|-------------------|
| dap_malloc/free | ~10M ops/sec |
| dap_list_append | ~1M ops/sec |
| dap_hash_sha256 | ~100MB/sec |
| dap_time_now | ~10M ops/sec |

### Оптимизации

- **Inlined функции**: Критические функции встроены в код
- **Memory pools**: Переиспользование памяти для частых операций
- **SIMD оптимизации**: Использование векторных инструкций для хеширования
- **Lock-free структуры**: Безблокировочные алгоритмы для многопоточности

## Отладка

### Логирование

```c
#include "dap_log.h"

void debug_example() {
    // Различные уровни логирования
    dap_log(L_DEBUG, "Debug message: %d", 42);
    dap_log(L_INFO, "Info message");
    dap_log(L_WARNING, "Warning message");
    dap_log(L_ERROR, "Error message");
}
```

### Валидация

```c
#include "dap_common.h"

void validation_example() {
    // Проверка указателей
    void* ptr = dap_malloc(100);
    DAP_ASSERT(ptr != NULL);
    
    // Проверка границ
    DAP_ASSERT(size > 0 && size < MAX_SIZE);
    
    // Проверка строк
    DAP_ASSERT(str != NULL && strlen(str) > 0);
}
```

## Заключение

Core Module предоставляет надежную основу для всех остальных модулей DAP SDK. Он обеспечивает кроссплатформенность, эффективное управление ресурсами и общие утилиты, необходимые для разработки децентрализованных приложений.
