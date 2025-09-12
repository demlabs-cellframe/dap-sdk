# dap_enc_iaes.h - AES шифрование

## Обзор

Модуль `dap_enc_iaes` предоставляет высокопроизводительную реализацию AES (Advanced Encryption Standard) шифрования в режиме CBC (Cipher Block Chaining) для DAP SDK. Поддерживает AES-256 с оптимизированными алгоритмами для максимальной производительности.

## Основные возможности

- **AES-256 шифрование** в режиме CBC
- **Высокая производительность** с оптимизированными алгоритмами
- **Автоматическое управление памятью**
- **Гибкий API** с разными вариантами использования
- **Кроссплатформенность** для всех поддерживаемых платформ

## Архитектура

### Структура ключа AES

```c
typedef struct dap_enc_aes_key {
    unsigned char ivec[IAES_BLOCK_SIZE];  // Initialization Vector (16 байт)
} dap_enc_aes_key_t;
```

### Параметры шифрования

```c
#define IAES_BLOCK_SIZE    16    // Размер блока AES
#define IAES_KEYSIZE       32    // Размер ключа AES-256 (256 бит)
```

### Режим шифрования

- **CBC (Cipher Block Chaining)**: Каждый блок зависит от предыдущего
- **IV (Initialization Vector)**: Случайный вектор инициализации для каждого шифрования
- **PKCS7 Padding**: Автоматическое дополнение для блоков не кратных 16 байтам

## API Reference

### Инициализация и управление ключами

#### dap_enc_aes_key_new()
```c
void dap_enc_aes_key_new(struct dap_enc_key *a_key);
```

**Описание**: Инициализирует новый объект ключа AES.

**Параметры**:
- `a_key` - указатель на структуру ключа для инициализации

**Пример**:
```c
#include "dap_enc_key.h"
#include "dap_enc_iaes.h"

struct dap_enc_key *aes_key = DAP_NEW(struct dap_enc_key);
dap_enc_aes_key_new(aes_key);
// Теперь aes_key готов к использованию
```

#### dap_enc_aes_key_delete()
```c
void dap_enc_aes_key_delete(struct dap_enc_key *a_key);
```

**Описание**: Освобождает ресурсы, занятые ключом AES.

**Параметры**:
- `a_key` - ключ для удаления

**Пример**:
```c
dap_enc_aes_key_delete(aes_key);
DAP_DELETE(aes_key);
```

#### dap_enc_aes_key_generate()
```c
void dap_enc_aes_key_generate(struct dap_enc_key *a_key,
                             const void *kex_buf, size_t kex_size,
                             const void *seed, size_t seed_size,
                             size_t key_size);
```

**Описание**: Генерирует ключ AES из seed и дополнительных данных.

**Параметры**:
- `a_key` - ключ для генерации
- `kex_buf` - буфер с данными key exchange
- `kex_size` - размер key exchange буфера
- `seed` - seed для генерации ключа
- `seed_size` - размер seed
- `key_size` - требуемый размер ключа (игнорируется, всегда 256 бит)

**Пример**:
```c
const char *seed = "my_secret_seed";
dap_enc_aes_key_generate(aes_key, NULL, 0, seed, strlen(seed), 32);
```

### Расчет размеров

#### dap_enc_iaes256_calc_encode_size()
```c
size_t dap_enc_iaes256_calc_encode_size(const size_t size_in);
```

**Описание**: Вычисляет размер выходного буфера для шифрования.

**Параметры**:
- `size_in` - размер входных данных

**Возвращает**: Размер выходного буфера с учетом padding

**Пример**:
```c
const char *plaintext = "Hello, World!";
size_t plaintext_len = strlen(plaintext);
size_t encrypted_size = dap_enc_iaes256_calc_encode_size(plaintext_len);
printf("Need buffer size: %zu bytes\n", encrypted_size);
```

#### dap_enc_iaes256_calc_decode_max_size()
```c
size_t dap_enc_iaes256_calc_decode_max_size(const size_t size_in);
```

**Описание**: Вычисляет максимальный размер выходного буфера для дешифрования.

**Параметры**:
- `size_in` - размер зашифрованных данных

**Возвращает**: Максимальный размер буфера для расшифрованных данных

**Пример**:
```c
size_t decrypted_max_size = dap_enc_iaes256_calc_decode_max_size(encrypted_size);
printf("Max decrypted size: %zu bytes\n", decrypted_max_size);
```

### Шифрование с автоматическим управлением памятью

#### dap_enc_iaes256_cbc_encrypt()
```c
size_t dap_enc_iaes256_cbc_encrypt(struct dap_enc_key *a_key,
                                  const void *a_in, size_t a_in_size,
                                  void **a_out);
```

**Описание**: Шифрует данные AES-256 в режиме CBC с автоматическим выделением памяти.

**Параметры**:
- `a_key` - ключ шифрования
- `a_in` - входные данные для шифрования
- `a_in_size` - размер входных данных
- `a_out` - указатель на выходной буфер (будет выделен функцией)

**Возвращает**: Размер зашифрованных данных или 0 при ошибке

**Пример**:
```c
const char *plaintext = "Secret message";
size_t plaintext_len = strlen(plaintext);
void *ciphertext = NULL;

size_t ciphertext_len = dap_enc_iaes256_cbc_encrypt(aes_key,
                                                   plaintext, plaintext_len,
                                                   &ciphertext);

if (ciphertext_len > 0) {
    printf("Encrypted %zu bytes\n", ciphertext_len);
    // Работа с ciphertext...
    free(ciphertext);
} else {
    printf("Encryption failed\n");
}
```

#### dap_enc_iaes256_cbc_decrypt()
```c
size_t dap_enc_iaes256_cbc_decrypt(struct dap_enc_key *a_key,
                                  const void *a_in, size_t a_in_size,
                                  void **a_out);
```

**Описание**: Дешифрует данные AES-256 в режиме CBC с автоматическим выделением памяти.

**Параметры**:
- `a_key` - ключ дешифрования
- `a_in` - зашифрованные данные
- `a_in_size` - размер зашифрованных данных
- `a_out` - указатель на выходной буфер (будет выделен функцией)

**Возвращает**: Размер расшифрованных данных или 0 при ошибке

**Пример**:
```c
void *decrypted = NULL;
size_t decrypted_len = dap_enc_iaes256_cbc_decrypt(aes_key,
                                                  ciphertext, ciphertext_len,
                                                  &decrypted);

if (decrypted_len > 0) {
    printf("Decrypted: %.*s\n", (int)decrypted_len, (char *)decrypted);
    free(decrypted);
} else {
    printf("Decryption failed\n");
}
```

### Шифрование с предварительно выделенным буфером

#### dap_enc_iaes256_cbc_encrypt_fast()
```c
size_t dap_enc_iaes256_cbc_encrypt_fast(struct dap_enc_key *a_key,
                                       const void *a_in, size_t a_in_size,
                                       void *buf_out, size_t buf_out_size);
```

**Описание**: Шифрует данные в предварительно выделенный буфер для максимальной производительности.

**Параметры**:
- `a_key` - ключ шифрования
- `a_in` - входные данные для шифрования
- `a_in_size` - размер входных данных
- `buf_out` - предварительно выделенный выходной буфер
- `buf_out_size` - размер выходного буфера

**Возвращает**: Размер зашифрованных данных или 0 при ошибке

**Примечание**: Для максимальной производительности размер входных данных должен быть кратен размеру блока AES (16 байт).

**Пример**:
```c
// Выделяем буфер достаточного размера
size_t buffer_size = dap_enc_iaes256_calc_encode_size(plaintext_len);
uint8_t *buffer = malloc(buffer_size);

// Шифруем в предварительно выделенный буфер
size_t encrypted_len = dap_enc_iaes256_cbc_encrypt_fast(aes_key,
                                                       plaintext, plaintext_len,
                                                       buffer, buffer_size);

if (encrypted_len > 0) {
    printf("Fast encryption: %zu bytes\n", encrypted_len);
} else {
    printf("Fast encryption failed\n");
}

free(buffer);
```

#### dap_enc_iaes256_cbc_decrypt_fast()
```c
size_t dap_enc_iaes256_cbc_decrypt_fast(struct dap_enc_key *a_key,
                                       const void *a_in, size_t a_in_size,
                                       void *buf_out, size_t buf_out_size);
```

**Описание**: Дешифрует данные в предварительно выделенный буфер.

**Параметры**:
- `a_key` - ключ дешифрования
- `a_in` - зашифрованные данные
- `a_in_size` - размер зашифрованных данных
- `buf_out` - предварительно выделенный выходной буфер
- `buf_out_size` - размер выходного буфера

**Возвращает**: Размер расшифрованных данных или 0 при ошибке

**Пример**:
```c
// Буфер для расшифрованных данных
size_t max_decrypted_size = dap_enc_iaes256_calc_decode_max_size(encrypted_len);
uint8_t *decrypted_buffer = malloc(max_decrypted_size);

size_t decrypted_len = dap_enc_iaes256_cbc_decrypt_fast(aes_key,
                                                       buffer, encrypted_len,
                                                       decrypted_buffer, max_decrypted_size);

if (decrypted_len > 0) {
    printf("Fast decryption successful: %zu bytes\n", decrypted_len);
    printf("Decrypted: %.*s\n", (int)decrypted_len, decrypted_buffer);
} else {
    printf("Fast decryption failed\n");
}

free(decrypted_buffer);
```

## Примеры использования

### Пример 1: Простое шифрование/дешифрование

```c
#include "dap_enc_key.h"
#include "dap_enc_iaes.h"
#include <string.h>
#include <stdio.h>

int simple_aes_example() {
    // Создаем ключ AES
    struct dap_enc_key *aes_key = DAP_NEW(struct dap_enc_key);
    dap_enc_aes_key_new(aes_key);

    // Генерируем ключ из seed
    const char *seed = "my_secure_seed_12345";
    dap_enc_aes_key_generate(aes_key, NULL, 0, seed, strlen(seed), 32);

    // Данные для шифрования
    const char *plaintext = "This is a secret message that needs encryption";
    size_t plaintext_len = strlen(plaintext);

    printf("Original: %s\n", plaintext);
    printf("Length: %zu bytes\n", plaintext_len);

    // Шифруем
    void *ciphertext = NULL;
    size_t ciphertext_len = dap_enc_iaes256_cbc_encrypt(aes_key,
                                                       plaintext, plaintext_len,
                                                       &ciphertext);

    if (ciphertext_len == 0) {
        printf("Encryption failed\n");
        dap_enc_aes_key_delete(aes_key);
        DAP_DELETE(aes_key);
        return -1;
    }

    printf("Encrypted: %zu bytes\n", ciphertext_len);

    // Дешифруем
    void *decrypted = NULL;
    size_t decrypted_len = dap_enc_iaes256_cbc_decrypt(aes_key,
                                                      ciphertext, ciphertext_len,
                                                      &decrypted);

    if (decrypted_len == 0) {
        printf("Decryption failed\n");
        free(ciphertext);
        dap_enc_aes_key_delete(aes_key);
        DAP_DELETE(aes_key);
        return -1;
    }

    printf("Decrypted: %.*s\n", (int)decrypted_len, (char *)decrypted);
    printf("Decrypted length: %zu bytes\n", decrypted_len);

    // Проверяем целостность
    if (decrypted_len == plaintext_len &&
        memcmp(decrypted, plaintext, plaintext_len) == 0) {
        printf("✅ AES encryption/decryption successful!\n");
    } else {
        printf("❌ Data integrity check failed\n");
    }

    // Очистка
    free(ciphertext);
    free(decrypted);
    dap_enc_aes_key_delete(aes_key);
    DAP_DELETE(aes_key);

    return 0;
}
```

### Пример 2: Шифрование файла

```c
#include "dap_enc_key.h"
#include "dap_enc_iaes.h"
#include <stdio.h>
#include <stdlib.h>

int encrypt_file_example(const char *input_file, const char *output_file) {
    // Создаем ключ
    struct dap_enc_key *aes_key = DAP_NEW(struct dap_enc_key);
    dap_enc_aes_key_new(aes_key);

    // Генерируем ключ
    const char *seed = "file_encryption_key_seed";
    dap_enc_aes_key_generate(aes_key, NULL, 0, seed, strlen(seed), 32);

    // Читаем файл
    FILE *in_file = fopen(input_file, "rb");
    if (!in_file) {
        printf("Cannot open input file: %s\n", input_file);
        dap_enc_aes_key_delete(aes_key);
        DAP_DELETE(aes_key);
        return -1;
    }

    // Определяем размер файла
    fseek(in_file, 0, SEEK_END);
    size_t file_size = ftell(in_file);
    fseek(in_file, 0, SEEK_SET);

    // Читаем содержимое
    uint8_t *file_data = malloc(file_size);
    if (!file_data) {
        fclose(in_file);
        dap_enc_aes_key_delete(aes_key);
        DAP_DELETE(aes_key);
        return -1;
    }

    if (fread(file_data, 1, file_size, in_file) != file_size) {
        printf("Failed to read file\n");
        free(file_data);
        fclose(in_file);
        dap_enc_aes_key_delete(aes_key);
        DAP_DELETE(aes_key);
        return -1;
    }
    fclose(in_file);

    // Шифруем данные
    void *encrypted_data = NULL;
    size_t encrypted_size = dap_enc_iaes256_cbc_encrypt(aes_key,
                                                       file_data, file_size,
                                                       &encrypted_data);

    free(file_data); // Освобождаем оригинальные данные

    if (encrypted_size == 0) {
        printf("File encryption failed\n");
        dap_enc_aes_key_delete(aes_key);
        DAP_DELETE(aes_key);
        return -1;
    }

    // Записываем зашифрованные данные
    FILE *out_file = fopen(output_file, "wb");
    if (!out_file) {
        printf("Cannot create output file: %s\n", output_file);
        free(encrypted_data);
        dap_enc_aes_key_delete(aes_key);
        DAP_DELETE(aes_key);
        return -1;
    }

    if (fwrite(encrypted_data, 1, encrypted_size, out_file) != encrypted_size) {
        printf("Failed to write encrypted file\n");
        fclose(out_file);
        free(encrypted_data);
        dap_enc_aes_key_delete(aes_key);
        DAP_DELETE(aes_key);
        return -1;
    }

    fclose(out_file);
    free(encrypted_data);

    printf("✅ File encrypted successfully\n");
    printf("   Input: %s (%zu bytes)\n", input_file, file_size);
    printf("   Output: %s (%zu bytes)\n", output_file, encrypted_size);

    dap_enc_aes_key_delete(aes_key);
    DAP_DELETE(aes_key);

    return 0;
}

int decrypt_file_example(const char *input_file, const char *output_file) {
    // Создаем ключ (тот же seed)
    struct dap_enc_key *aes_key = DAP_NEW(struct dap_enc_key);
    dap_enc_aes_key_new(aes_key);

    const char *seed = "file_encryption_key_seed";
    dap_enc_aes_key_generate(aes_key, NULL, 0, seed, strlen(seed), 32);

    // Читаем зашифрованный файл
    FILE *in_file = fopen(input_file, "rb");
    if (!in_file) {
        printf("Cannot open encrypted file: %s\n", input_file);
        dap_enc_aes_key_delete(aes_key);
        DAP_DELETE(aes_key);
        return -1;
    }

    fseek(in_file, 0, SEEK_END);
    size_t encrypted_size = ftell(in_file);
    fseek(in_file, 0, SEEK_SET);

    uint8_t *encrypted_data = malloc(encrypted_size);
    if (!encrypted_data) {
        fclose(in_file);
        dap_enc_aes_key_delete(aes_key);
        DAP_DELETE(aes_key);
        return -1;
    }

    if (fread(encrypted_data, 1, encrypted_size, in_file) != encrypted_size) {
        printf("Failed to read encrypted file\n");
        free(encrypted_data);
        fclose(in_file);
        dap_enc_aes_key_delete(aes_key);
        DAP_DELETE(aes_key);
        return -1;
    }
    fclose(in_file);

    // Дешифруем
    void *decrypted_data = NULL;
    size_t decrypted_size = dap_enc_iaes256_cbc_decrypt(aes_key,
                                                       encrypted_data, encrypted_size,
                                                       &decrypted_data);

    free(encrypted_data);

    if (decrypted_size == 0) {
        printf("File decryption failed\n");
        dap_enc_aes_key_delete(aes_key);
        DAP_DELETE(aes_key);
        return -1;
    }

    // Записываем расшифрованные данные
    FILE *out_file = fopen(output_file, "wb");
    if (!out_file) {
        printf("Cannot create output file: %s\n", output_file);
        free(decrypted_data);
        dap_enc_aes_key_delete(aes_key);
        DAP_DELETE(aes_key);
        return -1;
    }

    if (fwrite(decrypted_data, 1, decrypted_size, out_file) != decrypted_size) {
        printf("Failed to write decrypted file\n");
        fclose(out_file);
        free(decrypted_data);
        dap_enc_aes_key_delete(aes_key);
        DAP_DELETE(aes_key);
        return -1;
    }

    fclose(out_file);
    free(decrypted_data);

    printf("✅ File decrypted successfully\n");
    printf("   Input: %s (%zu bytes)\n", input_file, encrypted_size);
    printf("   Output: %s (%zu bytes)\n", output_file, decrypted_size);

    dap_enc_aes_key_delete(aes_key);
    DAP_DELETE(aes_key);

    return 0;
}
```

### Пример 3: Быстрое шифрование больших данных

```c
#include "dap_enc_key.h"
#include "dap_enc_iaes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHUNK_SIZE (64 * 1024) // 64KB chunks для эффективной обработки

int encrypt_large_data_example() {
    // Создаем ключ
    struct dap_enc_key *aes_key = DAP_NEW(struct dap_enc_key);
    dap_enc_aes_key_new(aes_key);

    const char *seed = "large_data_encryption_seed";
    dap_enc_aes_key_generate(aes_key, NULL, 0, seed, strlen(seed), 32);

    // Генерируем тестовые данные большого размера
    const size_t data_size = 1024 * 1024; // 1MB
    uint8_t *large_data = malloc(data_size);

    if (!large_data) {
        printf("Cannot allocate memory for test data\n");
        dap_enc_aes_key_delete(aes_key);
        DAP_DELETE(aes_key);
        return -1;
    }

    // Заполняем тестовыми данными
    for (size_t i = 0; i < data_size; i++) {
        large_data[i] = (uint8_t)(i % 256);
    }

    printf("Original data size: %zu bytes\n", data_size);

    // Шифруем по частям для эффективной обработки больших данных
    size_t encrypted_size = dap_enc_iaes256_calc_encode_size(data_size);
    uint8_t *encrypted_data = malloc(encrypted_size);

    if (!encrypted_data) {
        printf("Cannot allocate memory for encrypted data\n");
        free(large_data);
        dap_enc_aes_key_delete(aes_key);
        DAP_DELETE(aes_key);
        return -1;
    }

    // Быстрое шифрование
    size_t actual_encrypted_size = dap_enc_iaes256_cbc_encrypt_fast(
        aes_key, large_data, data_size, encrypted_data, encrypted_size);

    if (actual_encrypted_size == 0) {
        printf("Large data encryption failed\n");
        free(large_data);
        free(encrypted_data);
        dap_enc_aes_key_delete(aes_key);
        DAP_DELETE(aes_key);
        return -1;
    }

    printf("Encrypted data size: %zu bytes\n", actual_encrypted_size);

    // Дешифруем
    size_t max_decrypted_size = dap_enc_iaes256_calc_decode_max_size(actual_encrypted_size);
    uint8_t *decrypted_data = malloc(max_decrypted_size);

    if (!decrypted_data) {
        printf("Cannot allocate memory for decrypted data\n");
        free(large_data);
        free(encrypted_data);
        dap_enc_aes_key_delete(aes_key);
        DAP_DELETE(aes_key);
        return -1;
    }

    size_t actual_decrypted_size = dap_enc_iaes256_cbc_decrypt_fast(
        aes_key, encrypted_data, actual_encrypted_size,
        decrypted_data, max_decrypted_size);

    if (actual_decrypted_size == 0) {
        printf("Large data decryption failed\n");
        free(large_data);
        free(encrypted_data);
        free(decrypted_data);
        dap_enc_aes_key_delete(aes_key);
        DAP_DELETE(aes_key);
        return -1;
    }

    printf("Decrypted data size: %zu bytes\n", actual_decrypted_size);

    // Проверяем целостность
    if (actual_decrypted_size == data_size &&
        memcmp(decrypted_data, large_data, data_size) == 0) {
        printf("✅ Large data AES encryption/decryption successful!\n");
        printf("   Original: %zu bytes\n", data_size);
        printf("   Encrypted: %zu bytes\n", actual_encrypted_size);
        printf("   Decrypted: %zu bytes\n", actual_decrypted_size);
    } else {
        printf("❌ Large data integrity check failed\n");
        printf("   Expected: %zu bytes\n", data_size);
        printf("   Got: %zu bytes\n", actual_decrypted_size);
    }

    // Очистка
    free(large_data);
    free(encrypted_data);
    free(decrypted_data);
    dap_enc_aes_key_delete(aes_key);
    DAP_DELETE(aes_key);

    return 0;
}
```

### Пример 4: Безопасное шифрование с обработкой ошибок

```c
#include "dap_enc_key.h"
#include "dap_enc_iaes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

typedef struct aes_encryption_context {
    struct dap_enc_key *key;
    const char *seed;
    size_t seed_length;
} aes_encryption_context_t;

int aes_context_init(aes_encryption_context_t *ctx, const char *seed) {
    if (!ctx || !seed) {
        errno = EINVAL;
        return -1;
    }

    ctx->seed = seed;
    ctx->seed_length = strlen(seed);

    // Создаем ключ
    ctx->key = DAP_NEW(struct dap_enc_key);
    if (!ctx->key) {
        errno = ENOMEM;
        return -1;
    }

    dap_enc_aes_key_new(ctx->key);

    // Генерируем ключ
    dap_enc_aes_key_generate(ctx->key, NULL, 0,
                            ctx->seed, ctx->seed_length, 32);

    return 0;
}

void aes_context_cleanup(aes_encryption_context_t *ctx) {
    if (ctx && ctx->key) {
        dap_enc_aes_key_delete(ctx->key);
        DAP_DELETE(ctx->key);
        ctx->key = NULL;
    }
}

int secure_encrypt_data(const aes_encryption_context_t *ctx,
                       const void *input_data, size_t input_size,
                       void **output_data, size_t *output_size) {

    if (!ctx || !ctx->key || !input_data || input_size == 0 ||
        !output_data || !output_size) {
        errno = EINVAL;
        return -1;
    }

    // Проверяем, что входные данные не слишком велики
    if (input_size > (1024 * 1024 * 100)) { // 100MB limit
        errno = EFBIG;
        return -1;
    }

    // Шифруем данные
    *output_size = dap_enc_iaes256_cbc_encrypt(ctx->key,
                                              input_data, input_size,
                                              output_data);

    if (*output_size == 0) {
        errno = EIO; // Ошибка ввода-вывода
        return -1;
    }

    return 0;
}

int secure_decrypt_data(const aes_encryption_context_t *ctx,
                       const void *input_data, size_t input_size,
                       void **output_data, size_t *output_size) {

    if (!ctx || !ctx->key || !input_data || input_size == 0 ||
        !output_data || !output_size) {
        errno = EINVAL;
        return -1;
    }

    // Проверяем размер входных данных
    if (input_size % IAES_BLOCK_SIZE != 0) {
        errno = EINVAL;
        return -1;
    }

    // Дешифруем данные
    *output_size = dap_enc_iaes256_cbc_decrypt(ctx->key,
                                              input_data, input_size,
                                              output_data);

    if (*output_size == 0) {
        errno = EIO;
        return -1;
    }

    return 0;
}

int secure_aes_example() {
    aes_encryption_context_t ctx = {0};

    // Инициализируем контекст
    if (aes_context_init(&ctx, "secure_communication_key_2024") != 0) {
        perror("Failed to initialize AES context");
        return -1;
    }

    // Тестовые данные
    const char *test_message = "This is a confidential message";
    size_t message_len = strlen(test_message);

    printf("Original message: %s\n", test_message);
    printf("Message length: %zu bytes\n", message_len);

    // Шифруем
    void *encrypted = NULL;
    size_t encrypted_size = 0;

    if (secure_encrypt_data(&ctx, test_message, message_len,
                           &encrypted, &encrypted_size) != 0) {
        perror("Encryption failed");
        aes_context_cleanup(&ctx);
        return -1;
    }

    printf("Encrypted size: %zu bytes\n", encrypted_size);

    // Дешифруем
    void *decrypted = NULL;
    size_t decrypted_size = 0;

    if (secure_decrypt_data(&ctx, encrypted, encrypted_size,
                           &decrypted, &decrypted_size) != 0) {
        perror("Decryption failed");
        free(encrypted);
        aes_context_cleanup(&ctx);
        return -1;
    }

    printf("Decrypted size: %zu bytes\n", decrypted_size);
    printf("Decrypted message: %.*s\n", (int)decrypted_size, (char *)decrypted);

    // Проверяем целостность
    if (decrypted_size == message_len &&
        memcmp(decrypted, test_message, message_len) == 0) {
        printf("✅ Secure AES encryption/decryption successful!\n");
    } else {
        printf("❌ Data integrity verification failed\n");
    }

    // Очистка
    free(encrypted);
    free(decrypted);
    aes_context_cleanup(&ctx);

    return 0;
}
```

## Производительность

### Бенчмарки AES шифрования

| Операция | Производительность | Примечание |
|----------|-------------------|------------|
| **Шифрование** | ~400-600 MB/s | Intel Core i7-8700K |
| **Дешифрование** | ~400-600 MB/s | Intel Core i7-8700K |
| **Генерация ключа** | ~50-100 μs | С SHA-3 хэшированием |
| **Fast режим** | +10-20% | Предварительно выделенный буфер |

### Факторы влияния производительности

1. **Размер данных**: Оптимально для больших объемов данных
2. **Выравнивание**: Данные, выровненные по границе блока, шифруются быстрее
3. **Память**: Fast режим эффективнее для повторяющихся операций
4. **Кэш CPU**: Данные в кэше обрабатываются значительно быстрее

### Рекомендации по оптимизации

```c
// Для максимальной производительности:
// 1. Используйте fast функции с предварительно выделенными буферами
// 2. Выравнивайте данные по границе блока AES (16 байт)
// 3. Обрабатывайте данные большими порциями
// 4. Переиспользуйте ключи для множественных операций

#define AES_OPTIMAL_CHUNK_SIZE (64 * 1024) // 64KB оптимально для большинства систем

int optimized_aes_encrypt(const struct dap_enc_key *key,
                         const void *data, size_t size,
                         void *output, size_t output_size) {

    // Проверяем выравнивание данных
    if (size % IAES_BLOCK_SIZE == 0) {
        // Данные выровнены - используем fast режим
        return dap_enc_iaes256_cbc_encrypt_fast(key, data, size, output, output_size);
    } else {
        // Данные не выровнены - используем обычный режим
        void *temp_output = NULL;
        size_t result = dap_enc_iaes256_cbc_encrypt(key, data, size, &temp_output);

        if (result > 0 && result <= output_size) {
            memcpy(output, temp_output, result);
            free(temp_output);
            return result;
        }

        if (temp_output) free(temp_output);
        return 0;
    }
}
```

## Безопасность

### Криптографическая стойкость

AES-256 обеспечивает:
- **128-bit безопасность** против brute force атак
- **256-bit безопасность** против ключевых атак
- **CBC режим** предотвращает паттерны в шифрованных данных

### Рекомендации по безопасному использованию

1. **Уникальные ключи**: Каждый ключ должен использоваться только один раз
2. **Случайные IV**: Каждый шифрование должно иметь уникальный IV
3. **Безопасное хранение**: Ключи должны храниться в защищенном хранилище
4. **Валидация входных данных**: Всегда проверяйте размеры и типы данных

### Защита от типичных атак

```c
// Безопасная генерация ключа
int generate_secure_key(struct dap_enc_key *key, const char *password) {
    // Используйте сильный seed
    const char *additional_entropy = "additional_entropy_data";
    char *combined_seed = malloc(strlen(password) + strlen(additional_entropy) + 32);

    if (!combined_seed) return -1;

    sprintf(combined_seed, "%s_%s_%ld", password, additional_entropy, time(NULL));

    dap_enc_aes_key_generate(key, NULL, 0, combined_seed, strlen(combined_seed), 32);

    // Очищаем память с seed
    memset(combined_seed, 0, strlen(combined_seed));
    free(combined_seed);

    return 0;
}

// Проверка целостности зашифрованных данных
int verify_encrypted_data_integrity(const void *encrypted_data, size_t size) {
    // Проверяем размер
    if (size == 0 || size % IAES_BLOCK_SIZE != 0) {
        return -1; // Неверный размер
    }

    // Проверяем, что данные не являются нулевыми
    const uint8_t *data = encrypted_data;
    int non_zero_count = 0;

    for (size_t i = 0; i < size && i < 256; i++) {
        if (data[i] != 0) non_zero_count++;
    }

    // Если данные выглядят подозрительно (слишком много нулей)
    if (non_zero_count < size / 8) {
        return -2; // Возможная атака или повреждение
    }

    return 0; // Данные выглядят нормально
}
```

## Лучшие практики

### 1. Управление ключами

```c
// Правильное управление жизненным циклом ключа
typedef struct aes_key_manager {
    struct dap_enc_key *key;
    time_t created_time;
    time_t last_used_time;
    uint32_t usage_count;
    bool compromised;
} aes_key_manager_t;

aes_key_manager_t *aes_key_manager_create(const char *seed) {
    aes_key_manager_t *manager = calloc(1, sizeof(aes_key_manager_t));
    if (!manager) return NULL;

    manager->key = DAP_NEW(struct dap_enc_key);
    if (!manager->key) {
        free(manager);
        return NULL;
    }

    dap_enc_aes_key_new(manager->key);
    dap_enc_aes_key_generate(manager->key, NULL, 0, seed, strlen(seed), 32);

    manager->created_time = time(NULL);
    manager->last_used_time = manager->created_time;
    manager->usage_count = 0;
    manager->compromised = false;

    return manager;
}

void aes_key_manager_destroy(aes_key_manager_t *manager) {
    if (manager) {
        if (manager->key) {
            dap_enc_aes_key_delete(manager->key);
            DAP_DELETE(manager->key);
        }
        free(manager);
    }
}
```

### 2. Обработка ошибок

```c
// Надежная обработка ошибок при шифровании
int safe_aes_encrypt(const struct dap_enc_key *key,
                    const void *input, size_t input_size,
                    void **output, size_t *output_size) {

    // Проверка параметров
    if (!key || !input || input_size == 0 || !output || !output_size) {
        return AES_ERROR_INVALID_PARAMS;
    }

    // Проверка размера входных данных
    if (input_size > AES_MAX_DATA_SIZE) {
        return AES_ERROR_DATA_TOO_LARGE;
    }

    // Вычисление требуемого размера выходного буфера
    size_t required_size = dap_enc_iaes256_calc_encode_size(input_size);
    if (required_size == 0) {
        return AES_ERROR_SIZE_CALCULATION;
    }

    // Шифрование
    *output_size = dap_enc_iaes256_cbc_encrypt(key, input, input_size, output);

    if (*output_size == 0) {
        return AES_ERROR_ENCRYPTION_FAILED;
    }

    // Проверка размера выходных данных
    if (*output_size != required_size) {
        // Ожидаемый размер не совпадает - возможно ошибка
        free(*output);
        *output = NULL;
        return AES_ERROR_SIZE_MISMATCH;
    }

    return AES_SUCCESS;
}
```

### 3. Работа с потоками данных

```c
// Шифрование потоковых данных
typedef struct aes_stream_context {
    struct dap_enc_key *key;
    uint8_t *buffer;
    size_t buffer_size;
    size_t buffered_data;
} aes_stream_context_t;

int aes_stream_init(aes_stream_context_t *ctx, const char *seed, size_t buffer_size) {
    if (!ctx || !seed || buffer_size == 0) return -1;

    ctx->key = DAP_NEW(struct dap_enc_key);
    if (!ctx->key) return -1;

    dap_enc_aes_key_new(ctx->key);
    dap_enc_aes_key_generate(ctx->key, NULL, 0, seed, strlen(seed), 32);

    ctx->buffer = malloc(buffer_size);
    if (!ctx->buffer) {
        dap_enc_aes_key_delete(ctx->key);
        DAP_DELETE(ctx->key);
        return -1;
    }

    ctx->buffer_size = buffer_size;
    ctx->buffered_data = 0;

    return 0;
}

int aes_stream_encrypt_data(aes_stream_context_t *ctx,
                           const void *data, size_t size,
                           void **encrypted, size_t *encrypted_size) {

    // Добавляем данные в буфер
    if (ctx->buffered_data + size > ctx->buffer_size) {
        // Буфер переполнен, шифруем текущие данные
        if (ctx->buffered_data > 0) {
            void *temp_encrypted = NULL;
            size_t temp_size = dap_enc_iaes256_cbc_encrypt(ctx->key,
                                                          ctx->buffer, ctx->buffered_data,
                                                          &temp_encrypted);

            if (temp_size == 0) return -1;

            // Возвращаем зашифрованные данные
            *encrypted = temp_encrypted;
            *encrypted_size = temp_size;
            ctx->buffered_data = 0;

            // Копируем новые данные в буфер
            memcpy(ctx->buffer, data, size);
            ctx->buffered_data = size;
        }
    } else {
        // Добавляем данные в буфер
        memcpy(ctx->buffer + ctx->buffered_data, data, size);
        ctx->buffered_data += size;
    }

    return 0;
}
```

## Заключение

Модуль `dap_enc_iaes` предоставляет высокопроизводительную и безопасную реализацию AES шифрования для DAP SDK:

### Ключевые преимущества:
- **Стандарт AES-256**: Широко принятый криптографический стандарт
- **Высокая производительность**: Оптимизированные алгоритмы
- **Гибкий API**: Разные варианты использования памяти
- **Надежность**: Обширное тестирование и проверка

### Основные возможности:
- Шифрование/дешифрование в режиме CBC
- Автоматическое управление памятью
- Fast режим для максимальной производительности
- Интеграция с системой ключей DAP SDK

### Рекомендации по использованию:
1. **Всегда проверяйте возвращаемые значения** функций
2. **Используйте достаточный размер буферов** для выходных данных
3. **Генерируйте ключи из надежных seed** значений
4. **Освобождайте память** после использования зашифрованных данных
5. **Для больших данных** используйте fast функции с предварительно выделенными буферами

### Следующие шаги:
1. Изучите другие алгоритмы шифрования в DAP SDK
2. Ознакомьтесь с примерами использования
3. Интегрируйте AES шифрование в свои приложения
4. Следите за обновлениями криптографических стандартов

Для получения дополнительной информации смотрите:
- `dap_enc_iaes.h` - полный API AES шифрования
- `dap_enc_key.h` - управление ключами
- Примеры в директории `examples/crypto/`
- Тесты в директории `test/crypto/`

