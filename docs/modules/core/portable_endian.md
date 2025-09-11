# DAP Portable Endian (Портативные функции endianness)

## Обзор

Модуль `portable_endian` предоставляет унифицированный интерфейс для работы с порядком байтов (endianness) на различных платформах. Это критически важно для сетевых приложений и кроссплатформенной разработки.

## Назначение

Различные компьютерные архитектуры хранят многобайтовые значения в памяти по-разному:

- **Big-endian (BE)**: Старший байт хранится первым (Motorola 68k, PowerPC, SPARC)
- **Little-endian (LE)**: Младший байт хранится первым (Intel x86, ARM в большинстве случаев)

При сетевой передаче данных или работе с файлами необходимо конвертировать между host endianness и network endianness (всегда big-endian).

## Основные возможности

- **Кроссплатформенная поддержка**: Автоматическое определение платформы
- **16/32/64-битные конвертации**: Полный набор функций для всех размеров
- **Host ↔ Network конвертация**: Перевод в сетевой порядок байтов
- **Оптимизация**: Использование встроенных функций компилятора где возможно

## Поддерживаемые платформы

Модуль автоматически определяет платформу и использует оптимальные реализации:

### Linux/macOS (glibc)
```c
#include <endian.h>  // Автоматически предоставляет htobe16, be16toh и т.д.
```

### macOS (Darwin)
```c
#include <libkern/OSByteOrder.h>
#define htobe16(x) OSSwapHostToBigInt16(x)
#define be16toh(x) OSSwapBigToHostInt16(x)
// ... и аналогично для 32/64 бит
```

### Windows (MSVC)
```c
#include <windows.h>
#define htobe16(x) _byteswap_ushort(x)
#define be16toh(x) _byteswap_ushort(x)
// ... с использованием _byteswap_*
```

### GCC/Clang (fallback)
```c
#define htobe16(x) __builtin_bswap16(x)
#define be16toh(x) __builtin_bswap16(x)
// ... с использованием __builtin_bswap*
```

## API Функции

### 16-битные конвертации

```c
// Host to Big-endian
uint16_t htobe16(uint16_t host_16bit);

// Host to Little-endian
uint16_t htole16(uint16_t host_16bit);

// Big-endian to Host
uint16_t be16toh(uint16_t big_endian_16bit);

// Little-endian to Host
uint16_t le16toh(uint16_t little_endian_16bit);
```

### 32-битные конвертации

```c
// Host to Big-endian
uint32_t htobe32(uint32_t host_32bit);

// Host to Little-endian
uint32_t htole32(uint32_t host_32bit);

// Big-endian to Host
uint32_t be32toh(uint32_t big_endian_32bit);

// Little-endian to Host
uint32_t le32toh(uint32_t little_endian_32bit);
```

### 64-битные конвертации

```c
// Host to Big-endian
uint64_t htobe64(uint64_t host_64bit);

// Host to Little-endian
uint64_t htole64(uint64_t host_64bit);

// Big-endian to Host
uint64_t be64toh(uint64_t big_endian_64bit);

// Little-endian to Host
uint64_t le64toh(uint64_t little_endian_64bit);
```

## Использование

### Сетевая коммуникация

```c
#include "portable_endian.h"

// Отправка 32-битного значения в сеть
uint32_t host_value = 0x12345678;
uint32_t network_value = htobe32(host_value);
send(socket_fd, &network_value, sizeof(network_value), 0);

// Получение 32-битного значения из сети
uint32_t received_network_value;
recv(socket_fd, &received_network_value, sizeof(received_network_value), 0);
uint32_t host_value = be32toh(received_network_value);
```

### Работа с файлами

```c
// Чтение big-endian значения из файла
uint32_t file_value;
fread(&file_value, sizeof(file_value), 1, file);
uint32_t host_value = be32toh(file_value);  // Конвертация в host порядок

// Запись в файл в big-endian формате
uint32_t host_value = 12345;
uint32_t file_value = htobe32(host_value);
fwrite(&file_value, sizeof(file_value), 1, file);
```

### Работа с протоколами

```c
// Пример работы с TCP заголовком (big-endian поля)
struct tcp_header {
    uint16_t source_port;      // Big-endian в сети
    uint16_t dest_port;        // Big-endian в сети
    uint32_t sequence_num;     // Big-endian в сети
    // ...
};

// Конвертация полей заголовка
tcp_header->source_port = htobe16(local_port);
tcp_header->dest_port = htobe16(remote_port);
tcp_header->sequence_num = htobe32(sequence_number);
```

## Определения порядка байтов

Модуль определяет следующие константы:

```c
#define __BYTE_ORDER    BYTE_ORDER
#define __BIG_ENDIAN    BIG_ENDIAN
#define __LITTLE_ENDIAN LITTLE_ENDIAN
#define __PDP_ENDIAN    PDP_ENDIAN
```

Эти константы используются для условной компиляции:

```c
#if __BYTE_ORDER == __LITTLE_ENDIAN
    // Код для little-endian систем
#elif __BYTE_ORDER == __BIG_ENDIAN
    // Код для big-endian систем
#endif
```

## Особенности реализации

### Автоматическое определение платформы

Модуль использует препроцессор для определения платформы:

```c
#if (defined(_WIN16) || defined(_WIN32) || defined(_WIN64)) && !defined(__WINDOWS__)
# define __WINDOWS__
#endif

#if defined(__linux__) || defined(__CYGWIN__)
# include <endian.h>
#endif
```

### Оптимизация производительности

- **Встроенные функции**: Использование `__builtin_bswap*` в GCC/Clang
- **Ассемблерные инструкции**: `bswap` на x86, специальные инструкции на ARM
- **Константные выражения**: Компилятор может оптимизировать константные значения

### Безопасность типов

Все функции используют `static inline`, что обеспечивает:
- **Type safety**: Строгая типизация входных/выходных параметров
- **Zero overhead**: Инлайновые функции без накладных расходов на вызов
- **Compile-time checks**: Проверка типов на этапе компиляции

## Производительность

### Сравнение реализаций

| Метод | Процессор | Задержка (cycles) |
|-------|-----------|-------------------|
| builtin_bswap | x86-64 | 1-2 |
| _byteswap | MSVC | 1-2 |
| OSSwap | macOS | 2-3 |
| Software swap | Любая | 10-20 |

### Рекомендации по оптимизации

1. **Используйте для больших объемов данных**: Конвертация отдельных значений имеет overhead
2. **Кэшируйте результаты**: Для часто используемых значений
3. **Векторизация**: Для массивов данных рассмотрите SIMD инструкции

## Использование в DAP SDK

Модуль критически важен для:

- **Сетевых протоколов**: Все сетевые пакеты используют big-endian
- **Блокчейн данных**: Хеши и криптографические значения
- **Кроссплатформенности**: Обеспечение совместимости между архитектурами
- **Файловые форматы**: Сериализация/десериализация данных

## Связанные модули

- `dap_common.h` - Общие определения и макросы
- `dap_stream.h` - Потоковая передача данных
- `dap_net.h` - Сетевые операции

## Замечания по безопасности

- **Buffer overflows**: Проверяйте размеры буферов при работе с массивами
- **Alignment**: Убедитесь в правильном выравнивании данных
- **Constant time**: Функции выполняются за постоянное время (важно для криптографии)
- **Side channels**: Избегайте утечек информации через время выполнения

## Отладка и тестирование

### Проверка корректности

```c
// Тест для 32-битного значения
uint32_t test_value = 0x12345678;
uint32_t be_value = htobe32(test_value);
uint32_t back_to_host = be32toh(be_value);

assert(test_value == back_to_host);  // Должно быть true
```

### Отладочный вывод

```c
printf("Host endianness: %s\n",
       (__BYTE_ORDER == __LITTLE_ENDIAN) ? "Little-endian" : "Big-endian");

printf("Original: 0x%08x, BE: 0x%08x, Back: 0x%08x\n",
       test_value, be_value, back_to_host);
```

## Примеры из практики DAP SDK

### Работа с сетевыми пакетами

```c
// Структура сетевого пакета DAP
typedef struct dap_packet {
    uint16_t version;      // Big-endian
    uint16_t type;         // Big-endian
    uint32_t length;       // Big-endian
    uint8_t  data[];       // Переменная длина
} dap_packet_t;

// Сериализация пакета
packet->version = htobe16(DAP_PROTOCOL_VERSION);
packet->type = htobe16(packet_type);
packet->length = htobe32(data_length);

// Отправка
send(socket, packet, sizeof(dap_packet_t) + data_length, 0);
```

### Работа с блокчейн данными

```c
// Хеш в блокчейне (обычно big-endian для отображения)
uint8_t hash[32];  // SHA-256 hash
// ... вычисление хеша ...

// Для отображения в hex (big-endian)
for(int i = 0; i < 32; i++) {
    printf("%02x", hash[i]);
}
```

Этот модуль является фундаментальным для обеспечения кроссплатформенной совместимости и корректной работы сетевых протоколов в DAP SDK.
