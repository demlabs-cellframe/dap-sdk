# DAP Thread-Specific Data (TSD - Данные специфичные для потока)

## Обзор

Модуль `dap_tsd` предоставляет механизм для хранения и извлечения типизированных данных в бинарных потоках. Это позволяет сериализовывать структурированные данные с типами и размерами в непрерывный байтовый массив.

## Назначение

В сетевых протоколах и системах сериализации часто возникает необходимость передавать структурированные данные с сохранением их типов и размеров. TSD предоставляет эффективный способ:

- **Сериализовать типизированные данные** в бинарный поток
- **Десериализовать данные** с проверкой типов и размеров
- **Итерировать по сериализованным данным** с автоматической валидацией
- **Обеспечивать type safety** при работе с бинарными данными

## Основные возможности

- **Типизированная сериализация**: Каждый элемент данных имеет тип и размер
- **Безопасное извлечение**: Проверка типов и размеров при десериализации
- **Итерация по данным**: Автоматическое перемещение по сериализованному потоку
- **Поддержка строк**: Специальная обработка NULL-terminated строк
- **Компактный формат**: Минимальные накладные расходы на метаданные

## Структура данных

```c
// Основная структура TSD элемента
typedef struct dap_tsd {
    uint16_t type;        // Тип данных (пользовательский идентификатор)
    uint32_t size;        // Размер данных в байтах
    byte_t data[];        // Данные переменной длины
} DAP_ALIGN_PACKED dap_tsd_t;
```

## API Функции

### Создание TSD элементов

```c
// Создание TSD с бинарными данными
dap_tsd_t *dap_tsd_create(uint16_t a_type, const void *a_data, size_t a_data_size);

// Создание TSD со скалярным значением
#define dap_tsd_create_scalar(type, value) \
    dap_tsd_create(type, &value, sizeof(value))

// Создание TSD со строкой (автоматически добавляет NULL-терминатор)
#define dap_tsd_create_string(type, str) \
    dap_tsd_create(type, str, dap_strlen(str) + 1)
```

### Запись в поток

```c
// Запись TSD в байтовый массив
byte_t *dap_tsd_write(byte_t *a_ptr, uint16_t a_type, const void *a_data, size_t a_data_size);
```

### Поиск и извлечение

```c
// Поиск первого TSD элемента заданного типа
dap_tsd_t *dap_tsd_find(byte_t *a_data, size_t a_data_size, uint16_t a_type);

// Поиск всех TSD элементов заданного типа
dap_list_t *dap_tsd_find_all(byte_t *a_data, size_t a_data_size, uint16_t a_type);
```

### Извлечение данных

```c
// Извлечение скалярного значения
#define dap_tsd_get_scalar(a, typeconv) \
    (a->size >= sizeof(typeconv) ? *((typeconv*)a->data) : (typeconv){0})

// Извлечение объектного значения
#define dap_tsd_get_object(a, typeconv) \
    (a->size >= sizeof(typeconv) ? ((typeconv*)a->data) : (typeconv*){0})

// Извлечение строки с проверкой корректности
#define dap_tsd_get_string(a) \
    (((char*)a->data)[a->size-1] == '\0' ? (char*)a->data : DAP_TSD_CORRUPTED_STRING)

#define dap_tsd_get_string_const(a) \
    (((const char*)a->data)[a->size-1] == '\0' ? (const char*)a->data : DAP_TSD_CORRUPTED_STRING)
```

### Итерация по данным

```c
// Макрос для итерации по всем TSD элементам
#define dap_tsd_iter(iter, iter_size, data, total_size) \
    for (byte_t *l_pos = (byte_t*)(data), *l_end = l_pos + (total_size) > l_pos ? l_pos + (total_size) : l_pos; \
         !!(iter = l_pos < l_end - sizeof(dap_tsd_t) && l_pos <= l_end - (iter_size = dap_tsd_size((dap_tsd_t*)l_pos)) \
             ? (dap_tsd_t*)l_pos : NULL); \
         l_pos += iter_size)
```

### Служебные функции

```c
// Получение полного размера TSD элемента (включая заголовок)
#define dap_tsd_size(a) ((uint64_t)sizeof(dap_tsd_t) + (a)->size)

// Проверка корректности размера TSD в буфере
#define dap_tsd_size_check(a, offset, total_size) \
    ((total_size) - (offset) >= dap_tsd_size(a) && (total_size) - (offset) <= (total_size))
```

## Использование

### Базовая сериализация

```c
#include "dap_tsd.h"

// Создание различных типов данных
uint32_t int_value = 42;
const char *str_value = "Hello, World!";
double double_value = 3.14159;

// Сериализация в буфер
byte_t buffer[1024];
byte_t *ptr = buffer;

// Запись целого числа
ptr = dap_tsd_write(ptr, 1, &int_value, sizeof(int_value));

// Запись строки
ptr = dap_tsd_write(ptr, 2, str_value, strlen(str_value) + 1);

// Запись числа с плавающей точкой
ptr = dap_tsd_write(ptr, 3, &double_value, sizeof(double_value));

// Размер итогового буфера
size_t total_size = ptr - buffer;
```

### Десериализация и поиск

```c
// Поиск конкретного типа данных
dap_tsd_t *found_int = dap_tsd_find(buffer, total_size, 1);
if (found_int) {
    uint32_t recovered_int = dap_tsd_get_scalar(found_int, uint32_t);
    printf("Found integer: %u\n", recovered_int);
}

// Поиск строки
dap_tsd_t *found_str = dap_tsd_find(buffer, total_size, 2);
if (found_str) {
    const char *recovered_str = dap_tsd_get_string_const(found_str);
    printf("Found string: %s\n", recovered_str);
}
```

### Итерация по всем элементам

```c
// Итерация по всем TSD элементам
dap_tsd_t *iter;
size_t iter_size;

dap_tsd_iter(iter, iter_size, buffer, total_size) {
    if (!iter) break;

    printf("Type: %u, Size: %u\n", iter->type, iter->size);

    // Обработка в зависимости от типа
    switch(iter->type) {
        case 1: {
            uint32_t value = dap_tsd_get_scalar(iter, uint32_t);
            printf("Integer value: %u\n", value);
            break;
        }
        case 2: {
            const char *str = dap_tsd_get_string_const(iter);
            printf("String value: %s\n", str);
            break;
        }
        case 3: {
            double value = dap_tsd_get_scalar(iter, double);
            printf("Double value: %f\n", value);
            break;
        }
    }
}
```

## Особенности реализации

### Формат хранения

Каждый TSD элемент имеет следующий формат в памяти:

```
[uint16_t type][uint32_t size][byte_t data[size]]
```

### Выравнивание

Структура использует `DAP_ALIGN_PACKED` для плотной упаковки данных без дополнительных байтов выравнивания.

### Безопасность

- **Проверка границ**: Все операции проверяют доступное пространство
- **Type safety**: Строгая типизация при извлечении данных
- **NULL safety**: Защита от NULL указателей
- **String safety**: Проверка корректности NULL-terminated строк

## Производительность

### Накладные расходы

| Операция | Накладные расходы |
|----------|-------------------|
| Создание TSD | O(1) - копирование данных |
| Поиск элемента | O(n) - линейный поиск |
| Извлечение данных | O(1) - прямая адресация |
| Итерация | O(n) - последовательный обход |

### Оптимизации

- **Компактный формат**: Минимальные метаданные (6 байт на элемент)
- **Zero-copy**: Данные хранятся в оригинальном формате
- **Inline операции**: Многие операции выполняются без вызовов функций

## Использование в DAP SDK

TSD широко используется для:

- **Сетевых протоколов**: Сериализация сообщений между узлами
- **Блокчейн транзакций**: Хранение метаданных транзакций
- **Конфигурационные данные**: Типизированное хранение настроек
- **Кэширование**: Хранение структурированных данных в памяти

### Примеры использования

#### Сетевое сообщение

```c
// Структура сетевого сообщения
typedef enum {
    MSG_TYPE_HANDSHAKE = 1,
    MSG_TYPE_DATA = 2,
    MSG_TYPE_ACK = 3
} message_type_t;

// Создание handshake сообщения
uint32_t node_id = 12345;
const char *node_name = "Node-ABC";

byte_t *msg_buffer = malloc(1024);
byte_t *ptr = msg_buffer;

// Тип сообщения
ptr = dap_tsd_write(ptr, MSG_TYPE_HANDSHAKE, NULL, 0);

// ID узла
ptr = dap_tsd_write(ptr, 100, &node_id, sizeof(node_id));

// Имя узла
ptr = dap_tsd_write(ptr, 101, node_name, strlen(node_name) + 1);

// Отправка сообщения
size_t msg_size = ptr - msg_buffer;
send(socket, msg_buffer, msg_size, 0);
```

#### Блокчейн транзакция

```c
// Метаданные транзакции
typedef enum {
    TX_META_TYPE = 1,
    TX_AMOUNT_TYPE = 2,
    TX_TIMESTAMP_TYPE = 3,
    TX_SENDER_TYPE = 4
} tx_metadata_type_t;

// Создание метаданных транзакции
byte_t *metadata = malloc(512);
byte_t *ptr = metadata;

uint16_t tx_type = 1;  // Тип транзакции
uint256_t amount = dap_uint256_scan_decimal("100.0");
dap_time_t timestamp = dap_time_now();
const char *sender = "wallet_address_hash";

// Запись метаданных
ptr = dap_tsd_write(ptr, TX_META_TYPE, &tx_type, sizeof(tx_type));
ptr = dap_tsd_write(ptr, TX_AMOUNT_TYPE, &amount, sizeof(amount));
ptr = dap_tsd_write(ptr, TX_TIMESTAMP_TYPE, &timestamp, sizeof(timestamp));
ptr = dap_tsd_write(ptr, TX_SENDER_TYPE, sender, strlen(sender) + 1);
```

## Связанные модули

- `dap_common.h` - Общие типы и макросы
- `dap_list.h` - Работа со списками (используется для `dap_tsd_find_all`)
- `dap_stream.h` - Потоковая передача данных
- `dap_net.h` - Сетевые операции

## Замечания по безопасности

- **Buffer overflows**: Всегда проверяйте размеры буферов
- **Type validation**: Проверяйте типы данных при извлечении
- **Memory management**: Освобождайте память, выделенную для TSD элементов
- **Endianness**: Учитывайте порядок байтов при кроссплатформенной работе

## Отладка

### Проверка корректности данных

```c
// Функция для отладочного вывода TSD
void dap_tsd_debug_print(byte_t *data, size_t size) {
    dap_tsd_t *iter;
    size_t iter_size;

    printf("=== TSD Debug Info ===\n");
    dap_tsd_iter(iter, iter_size, data, size) {
        if (!iter) break;

        printf("Type: %u, Size: %u bytes\n", iter->type, iter->size);

        // Вывод первых 16 байт данных в hex
        printf("Data: ");
        for(size_t i = 0; i < MIN(16, iter->size); i++) {
            printf("%02x ", iter->data[i]);
        }
        printf("\n");
    }
    printf("=== End TSD Debug ===\n");
}
```

### Валидация целостности

```c
// Проверка целостности TSD потока
bool dap_tsd_validate(byte_t *data, size_t size) {
    dap_tsd_t *iter;
    size_t iter_size;
    size_t processed = 0;

    dap_tsd_iter(iter, iter_size, data, size) {
        if (!iter) {
            // Проверить, что обработали все данные
            return processed == size;
        }
        processed += iter_size;
    }

    return true;  // Все данные корректны
}
```

TSD является мощным инструментом для типизированной сериализации данных в DAP SDK, обеспечивая безопасность типов и эффективную работу с бинарными потоками.
