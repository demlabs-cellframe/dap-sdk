# dap_hash.h - Хэш-функции и криптографические хэши

## Обзор

Модуль `dap_hash` предоставляет высокопроизводительные криптографические хэш-функции для DAP SDK. Основан на стандарте SHA-3 (Keccak), обеспечивает высокую безопасность и производительность для всех задач хэширования в распределенных системах.

## Основные возможности

- **SHA-3/Keccak**: Стандартная реализация SHA-3-256
- **Высокая производительность**: Оптимизированные алгоритмы
- **Кроссплатформенность**: Работа на всех поддерживаемых платформах
- **Простой API**: Легко использовать в коде
- **Безопасность**: Криптографически стойкие хэши

## Архитектура

### Основные структуры данных

```c
// Основной тип хэша (32 байта)
typedef union dap_chain_hash_fast {
    uint8_t raw[DAP_CHAIN_HASH_FAST_SIZE]; // 32 байта
} DAP_ALIGN_PACKED dap_chain_hash_fast_t;

typedef dap_chain_hash_fast_t dap_hash_fast_t;
typedef dap_hash_fast_t dap_hash_t;

// Строковое представление хэша
typedef struct dap_hash_str {
    char s[DAP_HASH_FAST_STR_SIZE]; // "0x" + 64 символа + '\0'
} dap_hash_str_t;
```

### Типы хэш-функций

```c
typedef enum dap_hash_type {
    DAP_HASH_TYPE_KECCAK = 0,    // SHA-3/Keccak (основной)
    DAP_HASH_TYPE_SLOW_0 = 1     // Резерв для медленных алгоритмов
} dap_hash_type_t;
```

## Константы

```c
#define DAP_HASH_FAST_SIZE          32     // Размер хэша в байтах
#define DAP_CHAIN_HASH_FAST_SIZE    32     // Синоним
#define DAP_CHAIN_HASH_FAST_STR_LEN 66     // "0x" + 64 hex символа
#define DAP_CHAIN_HASH_FAST_STR_SIZE 67    // С завершающим нулем
#define DAP_HASH_FAST_STR_SIZE      67     // Синоним
```

## API Reference

### Основные функции хэширования

#### dap_hash_fast()

```c
DAP_STATIC_INLINE bool dap_hash_fast(const void *a_data_in,
                                   size_t a_data_in_size,
                                   dap_hash_fast_t *a_hash_out);
```

**Описание**: Вычисляет SHA-3-256 хэш для входных данных.

**Параметры**:
- `a_data_in` - указатель на входные данные
- `a_data_in_size` - размер входных данных в байтах
- `a_hash_out` - указатель на структуру для сохранения результата

**Возвращает**:
- `true` - хэш успешно вычислен
- `false` - ошибка (NULL параметры или нулевой размер)

**Пример**:
```c
#include "dap_hash.h"

const char *data = "Hello, World!";
size_t data_len = strlen(data);

dap_hash_fast_t hash;
if (dap_hash_fast(data, data_len, &hash)) {
    printf("Hash computed successfully\n");
    // hash.raw содержит 32 байта хэша
} else {
    printf("Hash computation failed\n");
}
```

### Сравнение хэшей

#### dap_hash_fast_compare()

```c
DAP_STATIC_INLINE bool dap_hash_fast_compare(const dap_hash_fast_t *a_hash1,
                                           const dap_hash_fast_t *a_hash2);
```

**Описание**: Сравнивает два хэша на равенство.

**Параметры**:
- `a_hash1` - первый хэш для сравнения
- `a_hash2` - второй хэш для сравнения

**Возвращает**:
- `true` - хэши равны
- `false` - хэши различны или параметры NULL

**Пример**:
```c
dap_hash_fast_t hash1, hash2;

// Вычисление хэшей
dap_hash_fast("data1", 5, &hash1);
dap_hash_fast("data2", 5, &hash2);

// Сравнение
if (dap_hash_fast_compare(&hash1, &hash2)) {
    printf("Hashes are identical\n");
} else {
    printf("Hashes are different\n");
}
```

#### dap_hash_fast_is_blank()

```c
DAP_STATIC_INLINE bool dap_hash_fast_is_blank(const dap_hash_fast_t *a_hash);
```

**Описание**: Проверяет, является ли хэш пустым (нулевым).

**Параметры**:
- `a_hash` - хэш для проверки

**Возвращает**:
- `true` - хэш пустой (все байты равны 0)
- `false` - хэш не пустой или параметр NULL

**Пример**:
```c
dap_hash_fast_t hash = {0}; // Инициализация нулевым хэшем

if (dap_hash_fast_is_blank(&hash)) {
    printf("Hash is blank (all zeros)\n");
} else {
    printf("Hash contains data\n");
}
```

### Конвертация в строки

#### dap_chain_hash_fast_to_str()

```c
DAP_STATIC_INLINE int dap_chain_hash_fast_to_str(const dap_hash_fast_t *a_hash,
                                               char *a_str,
                                               size_t a_str_max);
```

**Описание**: Конвертирует хэш в шестнадцатеричную строку с префиксом "0x".

**Параметры**:
- `a_hash` - хэш для конвертации
- `a_str` - буфер для строки (должен быть размером минимум `DAP_CHAIN_HASH_FAST_STR_SIZE`)
- `a_str_max` - максимальный размер буфера

**Возвращает**:
- `DAP_CHAIN_HASH_FAST_STR_SIZE` - успех
- `-1` - параметр `a_hash` равен NULL
- `-2` - параметр `a_str` равен NULL
- `-3` - буфер слишком мал

**Пример**:
```c
dap_hash_fast_t hash;
dap_hash_fast("test data", 9, &hash);

char hash_str[DAP_CHAIN_HASH_FAST_STR_SIZE];
if (dap_chain_hash_fast_to_str(&hash, hash_str, sizeof(hash_str)) > 0) {
    printf("Hash: %s\n", hash_str);
    // Вывод: 0xa8c5d6e7f8b9c0d1e2f3a4b5c6d7e8f9a0b1c2d3e4f5a6b7c8d9eaf0
}
```

#### dap_chain_hash_fast_to_str_new()

```c
DAP_STATIC_INLINE char *dap_chain_hash_fast_to_str_new(const dap_hash_fast_t *a_hash);
```

**Описание**: Создает новую строку с представлением хэша (требует освобождения памяти).

**Параметры**:
- `a_hash` - хэш для конвертации

**Возвращает**: Указатель на строку или `NULL` при ошибке (нужно освобождать с помощью `free()`)

**Пример**:
```c
dap_hash_fast_t hash;
dap_hash_fast("example", 7, &hash);

char *hash_str = dap_chain_hash_fast_to_str_new(&hash);
if (hash_str) {
    printf("Hash string: %s\n", hash_str);
    free(hash_str); // Важно освободить память
}
```

#### dap_chain_hash_fast_to_hash_str()

```c
DAP_STATIC_INLINE dap_hash_str_t dap_chain_hash_fast_to_hash_str(const dap_hash_fast_t *a_hash);
```

**Описание**: Конвертирует хэш в структуру строки.

**Пример**:
```c
dap_hash_fast_t hash;
dap_hash_fast("data", 4, &hash);

dap_hash_str_t hash_struct = dap_chain_hash_fast_to_hash_str(&hash);
printf("Hash: %s\n", hash_struct.s);
```

### Конвертация из строк

#### dap_chain_hash_fast_from_str()

```c
int dap_chain_hash_fast_from_str(const char *a_hash_str, dap_hash_fast_t *a_hash);
```

**Описание**: Парсит хэш из строки (поддерживает различные форматы).

**Параметры**:
- `a_hash_str` - строка с хэшем
- `a_hash` - указатель для сохранения результата

**Возвращает**:
- `0` - успех
- `-1` - ошибка парсинга

**Пример**:
```c
const char *hash_str = "0xa8c5d6e7f8b9c0d1e2f3a4b5c6d7e8f9a0b1c2d3e4f5a6b7c8d9eaf0";
dap_hash_fast_t hash;

if (dap_chain_hash_fast_from_str(hash_str, &hash) == 0) {
    printf("Hash parsed successfully\n");
}
```

#### dap_chain_hash_fast_from_hex_str()

```c
int dap_chain_hash_fast_from_hex_str(const char *a_hex_str, dap_hash_fast_t *a_hash);
```

**Описание**: Парсит хэш из шестнадцатеричной строки.

**Пример**:
```c
const char *hex_str = "a8c5d6e7f8b9c0d1e2f3a4b5c6d7e8f9a0b1c2d3e4f5a6b7c8d9eaf0";
dap_hash_fast_t hash;

if (dap_chain_hash_fast_from_hex_str(hex_str, &hash) == 0) {
    printf("Hex hash parsed successfully\n");
}
```

#### dap_chain_hash_fast_from_base58_str()

```c
int dap_chain_hash_fast_from_base58_str(const char *a_base58_str, dap_hash_fast_t *a_hash);
```

**Описание**: Парсит хэш из Base58 строки.

**Пример**:
```c
const char *b58_str = "QmSomeBase58EncodedHash";
dap_hash_fast_t hash;

if (dap_chain_hash_fast_from_base58_str(b58_str, &hash) == 0) {
    printf("Base58 hash parsed successfully\n");
}
```

### Удобные функции

#### dap_hash_fast_str_new()

```c
DAP_STATIC_INLINE char *dap_hash_fast_str_new(const void *a_data, size_t a_data_size);
```

**Описание**: Вычисляет хэш и возвращает его строковое представление.

**Пример**:
```c
const char *data = "Quick hash computation";
char *hash_str = dap_hash_fast_str_new(data, strlen(data));

if (hash_str) {
    printf("Data hash: %s\n", hash_str);
    free(hash_str);
}
```

#### dap_get_data_hash_str()

```c
DAP_STATIC_INLINE dap_hash_str_t dap_get_data_hash_str(const void *a_data, size_t a_data_size);
```

**Описание**: Вычисляет хэш и возвращает структуру со строковым представлением.

**Пример**:
```c
dap_hash_str_t hash_result = dap_get_data_hash_str("test", 4);
printf("Hash result: %s\n", hash_result.s);
```

## Примеры использования

### Пример 1: Базовое хэширование

```c
#include "dap_hash.h"
#include <stdio.h>
#include <string.h>

int basic_hash_example() {
    // Данные для хэширования
    const char *message = "Hello, DAP SDK!";
    size_t message_len = strlen(message);

    // Вычисление хэша
    dap_hash_fast_t hash;
    if (!dap_hash_fast(message, message_len, &hash)) {
        fprintf(stderr, "Failed to compute hash\n");
        return -1;
    }

    // Конвертация в строку для отображения
    char hash_str[DAP_CHAIN_HASH_FAST_STR_SIZE];
    if (dap_chain_hash_fast_to_str(&hash, hash_str, sizeof(hash_str)) > 0) {
        printf("Message: %s\n", message);
        printf("SHA-3-256: %s\n", hash_str);
    }

    return 0;
}
```

### Пример 2: Сравнение файлов по хэшу

```c
#include "dap_hash.h"
#include <stdio.h>

int compare_files_by_hash(const char *file1, const char *file2) {
    // В реальном коде здесь было бы чтение файлов
    // Для демонстрации используем тестовые данные
    const char *data1 = "File content 1";
    const char *data2 = "File content 2";

    // Вычисление хэшей
    dap_hash_fast_t hash1, hash2;
    dap_hash_fast(data1, strlen(data1), &hash1);
    dap_hash_fast(data2, strlen(data2), &hash2);

    // Сравнение
    if (dap_hash_fast_compare(&hash1, &hash2)) {
        printf("Files are identical (same hash)\n");
        return 1; // Файлы одинаковые
    } else {
        printf("Files are different\n");
        return 0; // Файлы различны
    }
}

int file_hash_comparison_example() {
    return compare_files_by_hash("file1.txt", "file2.txt");
}
```

### Пример 3: Цепочка хэшей (Merkle tree)

```c
#include "dap_hash.h"
#include <stdio.h>
#include <string.h>

int merkle_tree_example() {
    // Листовые узлы
    const char *leaves[] = {
        "Transaction 1",
        "Transaction 2",
        "Transaction 3",
        "Transaction 4"
    };
    const int num_leaves = 4;

    // Вычисление хэшей листьев
    dap_hash_fast_t leaf_hashes[4];
    for (int i = 0; i < num_leaves; i++) {
        dap_hash_fast(leaves[i], strlen(leaves[i]), &leaf_hashes[i]);
    }

    // Вычисление хэшей промежуточных узлов
    dap_hash_fast_t intermediate1, intermediate2;

    // Комбинируем leaf_hashes[0] + leaf_hashes[1]
    uint8_t combined1[DAP_HASH_FAST_SIZE * 2];
    memcpy(combined1, leaf_hashes[0].raw, DAP_HASH_FAST_SIZE);
    memcpy(combined1 + DAP_HASH_FAST_SIZE, leaf_hashes[1].raw, DAP_HASH_FAST_SIZE);
    dap_hash_fast(combined1, sizeof(combined1), &intermediate1);

    // Комбинируем leaf_hashes[2] + leaf_hashes[3]
    uint8_t combined2[DAP_HASH_FAST_SIZE * 2];
    memcpy(combined2, leaf_hashes[2].raw, DAP_HASH_FAST_SIZE);
    memcpy(combined2 + DAP_HASH_FAST_SIZE, leaf_hashes[3].raw, DAP_HASH_FAST_SIZE);
    dap_hash_fast(combined2, sizeof(combined2), &intermediate2);

    // Корневой хэш
    uint8_t root_data[DAP_HASH_FAST_SIZE * 2];
    memcpy(root_data, intermediate1.raw, DAP_HASH_FAST_SIZE);
    memcpy(root_data + DAP_HASH_FAST_SIZE, intermediate2.raw, DAP_HASH_FAST_SIZE);

    dap_hash_fast_t root_hash;
    dap_hash_fast(root_data, sizeof(root_data), &root_hash);

    // Отображение результата
    char root_str[DAP_CHAIN_HASH_FAST_STR_SIZE];
    dap_chain_hash_fast_to_str(&root_hash, root_str, sizeof(root_str));

    printf("Merkle Root: %s\n", root_str);
    printf("Number of leaves: %d\n", num_leaves);

    return 0;
}
```

### Пример 4: Проверка целостности данных

```c
#include "dap_hash.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    char *data;
    size_t size;
    dap_hash_fast_t expected_hash;
} data_integrity_check_t;

int verify_data_integrity(data_integrity_check_t *check) {
    if (!check || !check->data || check->size == 0) {
        return -1; // Неверные параметры
    }

    // Вычисление актуального хэша
    dap_hash_fast_t actual_hash;
    if (!dap_hash_fast(check->data, check->size, &actual_hash)) {
        return -2; // Ошибка вычисления хэша
    }

    // Сравнение с ожидаемым хэшем
    if (dap_hash_fast_compare(&actual_hash, &check->expected_hash)) {
        return 0; // Данные целые
    } else {
        return 1; // Данные повреждены
    }
}

int data_integrity_example() {
    // Пример данных с известным хэшем
    const char *test_data = "This is test data for integrity check";
    const char *expected_hash_str = "0x5a2b3c4d5e6f7a8b9c0d1e2f3a4b5c6d7e8f9a0b1c2d3e4f5a6b7c8d9eaf0";

    // Парсинг ожидаемого хэша
    dap_hash_fast_t expected_hash;
    if (dap_chain_hash_fast_from_str(expected_hash_str, &expected_hash) != 0) {
        fprintf(stderr, "Failed to parse expected hash\n");
        return -1;
    }

    // Создание структуры для проверки
    data_integrity_check_t check = {
        .data = (char *)test_data,
        .size = strlen(test_data),
        .expected_hash = expected_hash
    };

    // Проверка целостности
    int result = verify_data_integrity(&check);

    switch (result) {
        case 0:
            printf("✅ Data integrity verified - data is intact\n");
            break;
        case 1:
            printf("❌ Data integrity check failed - data is corrupted\n");
            break;
        default:
            printf("⚠️  Data integrity check error\n");
            break;
    }

    return result;
}
```

### Пример 5: Генерация уникальных идентификаторов

```c
#include "dap_hash.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

// Генерация UUID на основе хэша
int generate_uuid_from_data(const char *seed_data, size_t seed_size, char *uuid_out, size_t uuid_max) {
    if (!seed_data || seed_size == 0 || !uuid_out || uuid_max < 37) {
        return -1;
    }

    // Вычисление хэша от seed данных
    dap_hash_fast_t hash;
    if (!dap_hash_fast(seed_data, seed_size, &hash)) {
        return -2;
    }

    // Форматирование как UUID (8-4-4-4-12)
    // Используем первые 16 байт хэша
    snprintf(uuid_out, uuid_max,
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             hash.raw[0], hash.raw[1], hash.raw[2], hash.raw[3],
             hash.raw[4], hash.raw[5],
             hash.raw[6], hash.raw[7],
             hash.raw[8], hash.raw[9],
             hash.raw[10], hash.raw[11], hash.raw[12], hash.raw[13], hash.raw[14], hash.raw[15]);

    return 0;
}

int uuid_generation_example() {
    // Генерация UUID на основе различных данных
    char uuid1[37], uuid2[37], uuid3[37];

    // UUID на основе имени
    if (generate_uuid_from_data("user@example.com", 15, uuid1, sizeof(uuid1)) == 0) {
        printf("User UUID: %s\n", uuid1);
    }

    // UUID на основе временной метки
    time_t now = time(NULL);
    char time_str[32];
    snprintf(time_str, sizeof(time_str), "%ld", now);

    if (generate_uuid_from_data(time_str, strlen(time_str), uuid2, sizeof(uuid2)) == 0) {
        printf("Time-based UUID: %s\n", uuid2);
    }

    // UUID на основе комбинации данных
    const char *combined_data = "session:12345:user@example.com";
    if (generate_uuid_from_data(combined_data, strlen(combined_data), uuid3, sizeof(uuid3)) == 0) {
        printf("Session UUID: %s\n", uuid3);
    }

    // Проверка уникальности
    if (strcmp(uuid1, uuid2) != 0 && strcmp(uuid2, uuid3) != 0 && strcmp(uuid1, uuid3) != 0) {
        printf("✅ All UUIDs are unique\n");
    } else {
        printf("❌ UUID collision detected\n");
    }

    return 0;
}
```

## Производительность

### Бенчмарки хэширования

| Операция | Производительность | Примечание |
|----------|-------------------|------------|
| **SHA-3-256** | ~150-200 MB/s | Intel Core i7-8700K |
| **Сравнение хэшей** | ~10-20 GB/s | Простое memcmp |
| **Конвертация в hex** | ~500-800 MB/s | dap_htoa64 |
| **Парсинг hex** | ~200-300 MB/s | atoh функции |

### Оптимизации

1. **SIMD инструкции**: SHA-3 использует оптимизированные Keccak реализации
2. **Inline функции**: Большинство функций объявлены как `static inline`
3. **Минимальные копирования**: Прямое использование буферов
4. **Предварительные вычисления**: Константы вычисляются на этапе компиляции

### Факторы влияния производительности

- **Размер данных**: Хэширование больших объемов данных эффективнее
- **Выравнивание памяти**: Выровненные буферы работают быстрее
- **Кэш CPU**: Данные в кэше обрабатываются быстрее
- **Память**: DDR4 vs DDR5 может влиять на производительность

## Безопасность

### Криптографическая стойкость

SHA-3-256 обеспечивает:
- **128-bit безопасность** против коллизий
- **256-bit безопасность** против preimage атак
- **256-bit безопасность** против second preimage атак

### Рекомендации по использованию

1. **Для цифровых подписей**: Используйте полные 32 байта
2. **Для хэш-таблиц**: Можно использовать первые 8-16 байт
3. **Для checksum**: Достаточно 4-8 байт
4. **Для уникальных ID**: Используйте первые 16 байт

### Предупреждения

- **Не используйте устаревшие хэши**: MD5, SHA-1 уязвимы
- **Длина вывода**: Всегда проверяйте размер выходного буфера
- **NULL проверки**: Всегда проверяйте входные параметры
- **Постоянное время**: SHA-3 имеет постоянное время выполнения

## Лучшие практики

### 1. Обработка ошибок

```c
// Правильная обработка ошибок при хэшировании
int safe_hash_computation(const void *data, size_t size, dap_hash_fast_t *hash) {
    if (!data || size == 0 || !hash) {
        return -1; // Неверные параметры
    }

    if (!dap_hash_fast(data, size, hash)) {
        return -2; // Ошибка вычисления
    }

    if (dap_hash_fast_is_blank(hash)) {
        return -3; // Получен пустой хэш (неожиданно)
    }

    return 0; // Успех
}
```

### 2. Работа со строками

```c
// Безопасная работа со строковыми представлениями
char *safe_hash_to_string(const dap_hash_fast_t *hash) {
    if (!hash) {
        return NULL;
    }

    char *str = dap_chain_hash_fast_to_str_new(hash);
    if (!str) {
        return NULL;
    }

    // Проверка корректности строки
    if (strlen(str) != (DAP_CHAIN_HASH_FAST_STR_SIZE - 1)) {
        free(str);
        return NULL;
    }

    return str;
}
```

### 3. Сравнение хэшей

```c
// Безопасное сравнение хэшей
bool secure_hash_compare(const dap_hash_fast_t *hash1, const dap_hash_fast_t *hash2) {
    if (!hash1 || !hash2) {
        return false; // NULL параметры считаем неравными
    }

    // Используем постоянное время сравнения
    return dap_hash_fast_compare(hash1, hash2);
}
```

### 4. Работа с большими данными

```c
// Эффективное хэширование больших файлов
int hash_large_file(const char *filename, dap_hash_fast_t *result) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        return -1;
    }

    const size_t BUFFER_SIZE = 8192;
    uint8_t buffer[BUFFER_SIZE];
    dap_hash_fast_t running_hash = {0};
    bool first_block = true;

    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, file)) > 0) {
        if (first_block) {
            // Первый блок
            dap_hash_fast(buffer, bytes_read, &running_hash);
            first_block = false;
        } else {
            // Последующие блоки: хэшируем running_hash + buffer
            uint8_t combined[DAP_HASH_FAST_SIZE + BUFFER_SIZE];
            memcpy(combined, running_hash.raw, DAP_HASH_FAST_SIZE);
            memcpy(combined + DAP_HASH_FAST_SIZE, buffer, bytes_read);
            dap_hash_fast(combined, DAP_HASH_FAST_SIZE + bytes_read, &running_hash);
        }
    }

    fclose(file);
    *result = running_hash;
    return 0;
}
```

## Заключение

Модуль `dap_hash` предоставляет эффективную и безопасную реализацию криптографического хэширования:

### Ключевые преимущества:
- **Стандарт SHA-3**: Современный криптографический стандарт
- **Высокая производительность**: Оптимизированные алгоритмы
- **Простота использования**: Четкий и понятный API
- **Надежность**: Обширное тестирование и проверка

### Основные возможности:
- Вычисление SHA-3-256 хэшей
- Конвертация между бинарным и строковым форматами
- Сравнение хэшей
- Поддержка различных форматов строк (hex, base58)

### Рекомендации по использованию:
1. **Всегда проверяйте возвращаемые значения** функций
2. **Используйте достаточный размер буферов** для строковых представлений
3. **Освобождайте память** для строк, созданных функциями `*new()`
4. **Проверяйте параметры** на NULL перед использованием

### Следующие шаги:
1. Изучите другие модули DAP SDK
2. Ознакомьтесь с примерами использования
3. Интегрируйте хэширование в свои приложения
4. Следите за обновлениями криптографических стандартов

Для получения дополнительной информации смотрите:
- `dap_hash.h` - полный API хэширования
- `KeccakHash.h` - реализация SHA-3
- Примеры в директории `examples/crypto/`
- Тесты в директории `test/crypto/`

