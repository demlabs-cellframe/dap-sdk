# DAP SDK API Reference

## Обзор

API Reference содержит полную документацию по всем функциям, структурам данных и константам DAP SDK. Этот справочник предназначен для разработчиков, которые уже знакомы с основами SDK и нуждаются в детальной информации о конкретных API.

## Структура API

### Core Module API
Основные функции и структуры данных ядра SDK.

#### Инициализация и завершение
- `dap_init()` - Инициализация DAP SDK
- `dap_deinit()` - Завершение работы SDK
- `dap_get_version()` - Получение версии SDK

#### Управление памятью
- `dap_malloc()` - Выделение памяти
- `dap_free()` - Освобождение памяти
- `dap_calloc()` - Выделение памяти с обнулением
- `dap_realloc()` - Перераспределение памяти

#### Утилиты
- `dap_time_now()` - Текущее время
- `dap_hash_data()` - Вычисление хеша
- `dap_strcmp()` - Сравнение строк

### Crypto Module API
Криптографические функции и алгоритмы.

#### Управление ключами
```c
// Типы ключей
typedef enum dap_enc_key_type {
    DAP_ENC_KEY_TYPE_SIG_DILITHIUM = 21,    // Dilithium подписи
    DAP_ENC_KEY_TYPE_SIG_FALCON = 24,       // Falcon подписи
    DAP_ENC_KEY_TYPE_SIG_ECDSA = 26,        // ECDSA (legacy)
    DAP_ENC_KEY_TYPE_MLWE_KYBER = 17,       // Kyber KEM
    // ... другие типы
} dap_enc_key_type_t;

// Создание ключа
dap_enc_key_t* dap_enc_key_new_generate(dap_enc_key_type_t type,
                                       const void* seed, size_t seed_size,
                                       const void* kdf_key, size_t kdf_key_size,
                                       size_t key_size);

// Удаление ключа
void dap_enc_key_delete(dap_enc_key_t* key);
```

#### Шифрование и дешифрование
```c
// Шифрование данных
size_t dap_enc_code(dap_enc_key_t* key,
                   const void* in, size_t in_size,
                   void* out, size_t out_size_max,
                   dap_enc_data_type_t out_type);

// Дешифрование данных
size_t dap_enc_decode(dap_enc_key_t* key,
                     const void* in, size_t in_size,
                     void* out, size_t out_size_max,
                     dap_enc_data_type_t in_type);
```

#### Цифровые подписи
```c
// Создание подписи
size_t dap_enc_key_sign_get(dap_enc_key_t* key,
                           const void* data, size_t data_size,
                           void* signature, size_t signature_size);

// Проверка подписи
int dap_enc_key_sign_verify(dap_enc_key_t* key,
                           const void* data, size_t data_size,
                           const void* signature, size_t signature_size);
```

### Net Module API
Сетевые функции и коммуникации.

#### Серверные функции
```c
// Создание HTTP сервера
dap_http_server_t* dap_http_server_create(const char* addr, uint16_t port);

// Добавление обработчика
int dap_http_simple_proc_add(dap_http_server_t* server,
                           const char* url_path,
                           http_proc_func_t proc_func,
                           void* arg);

// Запуск сервера
int dap_http_server_start(dap_http_server_t* server);
```

#### Клиентские функции
```c
// Создание HTTP клиента
dap_client_t* dap_client_connect(const char* addr, uint16_t port);

// Отправка данных
int dap_client_send(dap_client_t* client, const void* data, size_t size);

// Получение данных
int dap_client_recv(dap_client_t* client, void* buffer, size_t size);
```

## Типы данных

### Основные типы
```c
// Безопасные целые числа
typedef uint8_t dap_byte_t;
typedef uint32_t dap_uint_t;
typedef int32_t dap_int_t;
typedef uint64_t dap_time_t;

// Хеши и адреса
typedef union dap_hash {
    uint8_t raw[32];           // Сырые байты
    struct {
        uint64_t part1, part2, part3, part4;
    } parts;
} dap_hash_t;

typedef struct dap_addr {
    uint8_t addr[20];          // Адрес (160 бит)
    uint8_t type;              // Тип адреса
} dap_addr_t;
```

### Структуры ключей
```c
typedef struct dap_enc_key {
    union {
        size_t priv_key_data_size;
        size_t shared_key_size;
    };
    union {
        void* priv_key_data;
        byte_t* shared_key;
    };
    size_t pub_key_data_size;
    void* pub_key_data;
    dap_enc_key_type_t type;
} dap_enc_key_t;
```

## Константы и перечисления

### Коды ошибок
```c
#define DAP_SUCCESS 0           // Успешное выполнение
#define DAP_ERROR -1            // Общая ошибка
#define DAP_ENOMEM -2           // Недостаточно памяти
#define DAP_EINVAL -3           // Неверные аргументы
#define DAP_ENOENT -4           // Объект не найден
```

### Типы данных шифрования
```c
typedef enum dap_enc_data_type {
    DAP_ENC_DATA_TYPE_RAW = 0,      // Сырые данные
    DAP_ENC_DATA_TYPE_B64 = 1,      // Base64
    DAP_ENC_DATA_TYPE_B64_URLSAFE = 2, // URL-safe Base64
} dap_enc_data_type_t;
```

## Макросы и утилиты

### Управление памятью
```c
// Безопасное выделение памяти
#define DAP_NEW(type) ((type*)dap_malloc(sizeof(type)))
#define DAP_NEW_SIZE(type, size) ((type*)dap_malloc(sizeof(type) * (size)))
#define DAP_DELETE(ptr) do { dap_free(ptr); ptr = NULL; } while(0)
#define DAP_NEW_ZERED(type) ((type*)dap_calloc(1, sizeof(type)))
```

### Отладка и логирование
```c
// Уровни логирования
typedef enum {
    L_DEBUG = 0,
    L_INFO = 1,
    L_WARNING = 2,
    L_ERROR = 3,
    L_CRITICAL = 4
} dap_log_level_t;

// Логирование
#define log_it(level, fmt, ...) dap_log(level, "[%s:%d] " fmt, __FILE__, __LINE__, ##__VA_ARGS__)
```

## Функции обратного вызова

### Типы callback функций
```c
// Callback для обработки HTTP запросов
typedef void (*http_proc_func_t)(dap_http_simple_request_t* request,
                                dap_http_simple_response_t* response,
                                void* arg);

// Callback для сетевых событий
typedef void (*dap_client_callback_t)(dap_client_t* client,
                                    int event_type,
                                    void* arg);
```

## Производительность

### Бенчмарки типичных операций
| Операция | Производительность | Примечание |
|----------|-------------------|------------|
| `dap_hash_sha256` | ~200 MB/s | Intel Core i7 |
| `dap_enc_code` (AES) | ~500 MB/s | 256-bit key |
| `dap_malloc`/`dap_free` | ~10M ops/s | Small blocks |
| `dap_time_now` | ~20M ops/s | High precision |

### Оптимизации
- Используйте `dap_calloc` для массивов
- Предварительно выделяйте память для частых операций
- Используйте SIMD инструкции для криптографии

## Потокобезопасность

### Thread-safe функции
- Все функции управления памятью
- Криптографические операции (кроме генерации ключей)
- Функции хеширования
- Сетевые функции (с ограничениями)

### Thread-unsafe функции
- Генерация ключей (используйте mutex)
- Глобальные состояния
- Некоторые сетевые операции

## Совместимость

### Поддерживаемые платформы
- **Linux**: Полная поддержка
- **macOS**: Полная поддержка
- **Windows**: Ограниченная поддержка
- **Android**: Через JNI

### Версии компиляторов
- **GCC**: 7.0+
- **Clang**: 5.0+
- **MSVC**: 2017+

## Примеры использования

### Полный пример криптографических операций
```c
#include "dap_enc.h"
#include "dap_enc_key.h"

int crypto_operations_example() {
    // Инициализация
    if (dap_enc_init() != 0) return -1;

    // Создание ключа Dilithium
    dap_enc_key_t* key = dap_enc_key_new_generate(
        DAP_ENC_KEY_TYPE_SIG_DILITHIUM,
        NULL, 0, "seed", 4, 0
    );

    if (!key) {
        dap_enc_deinit();
        return -1;
    }

    // Данные для подписи
    const char* data = "Hello, DAP SDK!";
    size_t data_size = strlen(data);

    // Вычисление размера подписи
    size_t sig_size = dap_enc_calc_signature_unserialized_size(key);
    void* signature = dap_malloc(sig_size);

    // Создание подписи
    size_t actual_sig_size = key->sign_get(key, data, data_size,
                                         signature, sig_size);

    // Проверка подписи
    int verify_result = key->sign_verify(key, data, data_size,
                                       signature, actual_sig_size);

    // Очистка
    dap_free(signature);
    dap_enc_key_delete(key);
    dap_enc_deinit();

    return verify_result == 0 ? 0 : -1;
}
```

## Навигация

- **[Core Module](../modules/core.md)** - Подробная документация ядра
- **[Crypto Module](../modules/crypto.md)** - Криптографические функции
- **[Net Module](../modules/net.md)** - Сетевые функции
- **[Примеры](../examples/)** - Практические примеры
