# Примеры использования DAP SDK

## Обзор

В этой директории находятся практические примеры использования DAP SDK. Каждый пример демонстрирует конкретную функциональность и может быть использован как отправная точка для разработки приложений.

## Структура примеров

### Базовые примеры
- `hello_world/` - Простейший пример инициализации SDK
- `basic_crypto/` - Основы криптографических операций
- `simple_server/` - Простой HTTP сервер

### Продвинутые примеры
- `crypto_operations/` - Полные криптографические операции
- `network_communication/` - Сетевое взаимодействие
- `key_management/` - Управление ключами

### Специализированные примеры
- `post_quantum_crypto/` - Пост-квантовые алгоритмы
- `secure_communication/` - Безопасная коммуникация
- `performance_test/` - Тестирование производительности

## Быстрый старт

### Компиляция примеров

```bash
# Перейти в директорию примера
cd examples/hello_world

# Создать билд директорию
mkdir build && cd build

# Сконфигурировать и собрать
cmake ..
make

# Запустить
./hello_world
```

### Общая структура примера

Каждый пример содержит:
- `CMakeLists.txt` - Скрипт сборки
- `main.c` - Основной код примера
- `README.md` - Описание примера и инструкции

## Основные примеры

### 1. Hello World

```c
#include "dap_common.h"

int main() {
    // Инициализация DAP SDK
    if (dap_init() != 0) {
        printf("Failed to initialize DAP SDK\n");
        return -1;
    }

    printf("Hello, DAP SDK World!\n");

    // Очистка ресурсов
    dap_deinit();
    return 0;
}
```

### 2. Криптографические операции

```c
#include "dap_enc.h"
#include "dap_enc_key.h"

int crypto_example() {
    // Инициализация
    dap_enc_init();

    // Создание ключа
    dap_enc_key_t *key = dap_enc_key_new_generate(
        DAP_ENC_KEY_TYPE_SIG_FALCON, // Пост-квантовый алгоритм
        NULL, 0, "seed", 4, 0
    );

    // Подпись сообщения
    const char *message = "Hello, secure world!";
    uint8_t signature[1024];
    size_t sig_size = key->sign_get(key, message, strlen(message),
                                   signature, sizeof(signature));

    // Проверка подписи
    int verify_result = key->sign_verify(key, message, strlen(message),
                                       signature, sig_size);

    // Очистка
    dap_enc_key_delete(key);
    dap_enc_deinit();

    return verify_result == 0 ? 0 : -1;
}
```

### 3. Сетевой сервер

```c
#include "dap_http_server.h"

int server_example() {
    // Инициализация
    dap_http_init();

    // Создание сервера
    dap_http_server_t *server = dap_http_server_create("0.0.0.0", 8080);

    // Добавление обработчика
    dap_http_simple_proc_add(server, "/api/status",
                           status_handler, NULL);

    // Запуск сервера
    dap_http_server_start(server);

    // Основной цикл
    while (running) {
        sleep(1);
    }

    // Очистка
    dap_http_server_delete(server);
    dap_http_deinit();

    return 0;
}
```

## Сборка всех примеров

```bash
# Из корневой директории DAP SDK
mkdir build && cd build
cmake -DBUILD_DAP_SDK_EXAMPLES=ON ..
make

# Запуск конкретного примера
./examples/hello_world/hello_world
./examples/crypto_operations/crypto_test
```

## Требования

- **Компилятор**: GCC 7.0+ или Clang 5.0+
- **CMake**: 3.10+
- **Зависимости**: OpenSSL, libcurl (опционально)

## Советы по разработке

### 1. Всегда проверяйте возвращаемые значения
```c
if (dap_enc_init() != 0) {
    // Обработка ошибки
    return -1;
}
```

### 2. Правильно управляйте ресурсами
```c
dap_enc_key_t *key = dap_enc_key_new_generate(...);
// Использование ключа
dap_enc_key_delete(key); // Важно: очистка ресурсов
```

### 3. Используйте пост-квантовые алгоритмы
```c
// Рекомендуется для новых проектов
DAP_ENC_KEY_TYPE_SIG_FALCON    // Подписи
DAP_ENC_KEY_TYPE_MLWE_KYBER    // KEM

// Избегайте в новых проектах
DAP_ENC_KEY_TYPE_SIG_ECDSA     // Уязвим к квантовым атакам
```

## Получение помощи

- **Документация**: [docs/README.md](../README.md)
- **API Reference**: [docs/modules/](../modules/)
- **Архитектура**: [docs/architecture.md](../architecture.md)

## Лицензия

Примеры распространяются под той же лицензией, что и DAP SDK.
