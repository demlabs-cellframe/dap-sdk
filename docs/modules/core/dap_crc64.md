# DAP CRC64 Module (dap_crc64.h/c)

## Обзор

Модуль `dap_crc64.h/c` предоставляет высокопроизводительную реализацию алгоритмов циклического избыточного кода (CRC64) для DAP SDK. Модуль поддерживает несколько стандартов CRC64 и оптимизирован для различных платформ и архитектур.

## Назначение

CRC64 алгоритмы используются для:

- **Обнаружения ошибок** в передаваемых данных
- **Проверки целостности** файлов и сообщений
- **Генерации контрольных сумм** для сетевых протоколов
- **Валидации данных** в распределенных системах

## Архитектура

```mermaid
graph TB
    subgraph "DAP CRC64 Architecture"
        A[CRC64 Standards] --> B[ECMA-182]
        A --> C[ISO 3309]
        A --> D[Microsoft]
        A --> E[Redis]

        F[Core Functions] --> G[crc64_init()]
        F --> H[crc64_update()]
        F --> I[crc64()]

        J[Platform Optimizations] --> K[x86 SIMD]
        J --> L[ARM NEON]
        J --> M[Generic C]
    end
```

## Поддерживаемые стандарты CRC64

### 📊 **ECMA-182**
- **Полином**: 0x42f0e1eba9ea3693ULL
- **Начальное значение**: 0x0000000000000000ULL
- **Финальное XOR**: 0x0000000000000000ULL
- **Отражение**: Нет
- **Применение**: Стандарт ECMA для оптических дисков

### 🏛️ **ISO 3309**
- **Полином**: 0x0000000000000001ULL
- **Начальное значение**: 0xFFFFFFFFFFFFFFFFULL
- **Финальное XOR**: 0xFFFFFFFFFFFFFFFFULL
- **Отражение**: Да
- **Применение**: Международный стандарт ISO

### 🪟 **Microsoft**
- **Полином**: 0x259c84cba6426349ULL
- **Начальное значение**: 0xFFFFFFFFFFFFFFFFULL
- **Финальное XOR**: 0x0000000000000000ULL
- **Отражение**: Да
- **Применение**: Используется в Windows и .NET

### 🔴 **Redis**
- **Полином**: 0xad93d23594c935a9ULL
- **Начальное значение**: 0x0000000000000000ULL
- **Финальное XOR**: 0x0000000000000000ULL
- **Отражение**: Да
- **Применение**: Хеширование ключей в Redis кластере

## API Reference

### Инициализация

```c
// Инициализация CRC64 модуля
int dap_crc64_init(void);
```

### Основные функции

```c
// Вычисление CRC64 для блока данных
uint64_t crc64(const uint8_t *a_ptr, const size_t a_count);

// Инкрементальное обновление CRC
uint64_t crc64_update(uint64_t a_crc, const uint8_t *a_ptr, const size_t a_count);
```

### Внутренняя структура

```c
// Параметры CRC64 алгоритма
struct crc64_params {
    uint64_t poly;      // Полином
    uint64_t init;      // Начальное значение
    bool reflected;     // Отражение битов
    uint64_t xorout;    // Финальное XOR
    uint64_t check;     // Тестовое значение
};

// Предопределенные параметры
#define CRC64_PARAMS_ECMA182    {CRC64_ECMA182_POLY, UINT64_C(0), false, UINT64_C(0), 0x6c40df5f0b497347ULL}
#define CRC64_PARAMS_ISO        {CRC64_ISO_POLY_INV, UINT64_C(~0), true, UINT64_C(~0), 0xb90956c775a41001ULL}
#define CRC64_PARAMS_MS         {CRC64_MS_POLY_INV, UINT64_C(~0), true, UINT64_C(0), 0x75d4b74f024eceeaULL}
#define CRC64_PARAMS_REDIS      {CRC64_REDIS_POLY_INV, UINT64_C(0), true, UINT64_C(0), 0xe9c6d914c4b8d9caULL}
```

## Примеры использования

### Базовое вычисление CRC64

```c
#include "dap_crc64.h"

// Инициализация модуля
if (dap_crc64_init() != 0) {
    fprintf(stderr, "Failed to initialize CRC64\n");
    return -1;
}

// Вычисление CRC64 для строки
const char *data = "Hello, World!";
uint64_t crc = crc64((const uint8_t *)data, strlen(data));
printf("CRC64: 0x%016llx\n", crc);
```

### Инкрементальное вычисление

```c
#include "dap_crc64.h"

// Начало с нулевого CRC
uint64_t crc = 0;

// Добавление первого блока
const char *block1 = "First block";
crc = crc64_update(crc, (const uint8_t *)block1, strlen(block1));

// Добавление второго блока
const char *block2 = "Second block";
crc = crc64_update(crc, (const uint8_t *)block2, strlen(block2));

printf("Combined CRC64: 0x%016llx\n", crc);
```

### Проверка целостности файла

```c
#include "dap_crc64.h"
#include <stdio.h>

uint64_t calculate_file_crc(const char *filename) {
    FILE *file = fopen(filename, "rb");
    if (!file) return 0;

    uint8_t buffer[4096];
    uint64_t crc = 0;
    size_t bytes_read;

    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        crc = crc64_update(crc, buffer, bytes_read);
    }

    fclose(file);
    return crc;
}

// Использование
uint64_t file_crc = calculate_file_crc("data.bin");
printf("File CRC64: 0x%016llx\n", file_crc);
```

### Сравнение алгоритмов

```c
#include "dap_crc64.h"

// Тестовая строка
const char *test_data = "Test data for CRC64";
size_t data_len = strlen(test_data);

// Вычисление разными алгоритмами
uint64_t crc_ecma = crc64_ecma182((const uint8_t *)test_data, data_len);
uint64_t crc_iso = crc64_iso((const uint8_t *)test_data, data_len);
uint64_t crc_ms = crc64_ms((const uint8_t *)test_data, data_len);

printf("ECMA-182: 0x%016llx\n", crc_ecma);
printf("ISO:      0x%016llx\n", crc_iso);
printf("MS:       0x%016llx\n", crc_ms);
```

## Производительность

### 📈 **Оптимизации**
- **SIMD инструкции**: Использование SSE4.2, AVX2, ARM NEON
- **Таблица поиска**: Предвычисленные таблицы для быстрого расчета
- **Инкрементальные обновления**: Поддержка потоковой обработки
- **Платформо-специфичный код**: Оптимизации для разных архитектур

### 📊 **Производительность по платформам**

| Платформа | Производительность | Оптимизации |
|-----------|-------------------|-------------|
| x86-64 SSE4.2 | ~15 GB/s | SIMD, таблицы |
| x86-64 AVX2 | ~25 GB/s | Расширенный SIMD |
| ARM64 NEON | ~8 GB/s | Векторные инструкции |
| Generic C | ~1 GB/s | Базовая реализация |

### ⚡ **Сравнение с другими алгоритмами**

| Алгоритм | Размер хеша | Производительность | Надежность |
|----------|-------------|-------------------|------------|
| CRC32 | 32 бит | ~20 GB/s | Средняя |
| CRC64 | 64 бит | ~15 GB/s | Высокая |
| MD5 | 128 бит | ~5 GB/s | Средняя |
| SHA-256 | 256 бит | ~1 GB/s | Высокая |

## Безопасность

### 🔒 **Криптографическая стойкость**
- **Не предназначен для криптографии**: CRC не обеспечивает криптографическую безопасность
- **Подвержен коллизиям**: Возможны преднамеренные коллизии
- **Не устойчив к атакам**: Не защищает от преднамеренного повреждения

### ⚠️ **Предупреждения**
- Не используйте CRC для криптографических целей
- Для криптографической защиты используйте SHA-256 или выше
- CRC подходит только для обнаружения случайных ошибок
- Регулярно проверяйте целостность данных с помощью CRC

## Интеграция с другими модулями

### 🔗 **Зависимости**
- **dap_common.h**: Базовые типы и макросы
- **dap_endian.h**: Обработка порядка байтов
- **Платформо-специфичные заголовки**: Для SIMD оптимизаций

### 🔄 **Взаимодействие**
- **dap_hash.h**: Совместное использование для многоуровневой защиты
- **dap_file_utils.h**: Проверка целостности файлов
- **dap_stream.h**: Проверка целостности потоков данных
- **dap_global_db.h**: Проверка целостности записей БД

## Тестирование

### 🧪 **Набор тестов**
```bash
# Запуск всех тестов CRC64
make test_crc64

# Тестирование конкретного алгоритма
make test_crc64_ecma182
make test_crc64_iso
make test_crc64_ms

# Тестирование производительности
make benchmark_crc64

# Тестирование на разных платформах
make test_crc64_platforms
```

### ✅ **Критерии качества**
- Корректность вычисления контрольных сумм
- Соответствие стандартам CRC64
- Производительность операций
- Корректность инкрементальных обновлений
- Совместимость между платформами

## Отладка и мониторинг

### 🔍 **Отладочные функции**
```c
// Логирование операций CRC64
DAP_LOG_DEBUG("CRC64 calculation: ptr=%p, count=%zu, result=0x%016llx",
              a_ptr, a_count, result);

// Мониторинг производительности
DAP_LOG_DEBUG("CRC64 performance: %zu bytes in %llu microseconds",
              total_bytes, elapsed_time);
```

### 📊 **Метрики**
- Количество вычисленных CRC
- Среднее время вычисления
- Общий объем обработанных данных
- Процент использования SIMD оптимизаций

## Применение в DAP SDK

### 🔗 **Использование в компонентах**

#### Global DB
```c
// Проверка целостности записей БД
uint64_t record_crc = crc64(record_data, record_size);
if (record_crc != stored_crc) {
    DAP_LOG_ERROR("Database record corrupted");
}
```

#### File Utils
```c
// Верификация загружаемых файлов
uint64_t file_crc = calculate_file_crc(filename);
if (file_crc != expected_crc) {
    DAP_LOG_ERROR("File integrity check failed");
}
```

#### Network Streams
```c
// Проверка целостности сетевых пакетов
uint64_t packet_crc = crc64_update(stream_crc, packet_data, packet_size);
// Продолжение обработки потока
```

## Будущие улучшения

### 🚀 **Планы развития**
- **Дополнительные стандарты**: Поддержка CRC64-Jones, CRC64-XZ
- **Аппаратное ускорение**: Интеграция с Intel CRC32C, ARM CRC32
- **Расширенные оптимизации**: AVX-512, SVE для ARM
- **Кастомные полиномы**: Поддержка пользовательских CRC64

### 🔮 **Исследуемые технологии**
- **SIMD оптимизации**: Новые векторные инструкции
- **Многопоточная обработка**: Параллельное вычисление CRC
- **Memory-mapped files**: Оптимизация для больших файлов
- **GPU ускорение**: Вычисление CRC на GPU

---

*Этот документ является частью технической документации DAP SDK. Для получения дополнительной информации обратитесь к документации сетевых протоколов или к команде разработчиков.*
