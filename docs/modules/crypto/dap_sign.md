# dap_sign.h - Цифровые подписи и криптографическая аутентификация

## ⚠️ **ВАЖНОЕ ПРЕДУПРЕЖДЕНИЕ О БЕЗОПАСНОСТИ**

# 🚨 **КВАНТОВАЯ НЕБЕЗОПАСНОСТЬ КЛАССИЧЕСКИХ АЛГОРИТМОВ**

**Некоторые алгоритмы цифровых подписей в DAP SDK являются классическими криптографическими методами и НЕ ОБЕСПЕЧИВАЮТ ЗАЩИТУ ОТ АТАК КВАНТОВЫХ КОМПЬЮТЕРОВ.**

### **Критические предупреждения:**
- ✅ **Пост-квантовые алгоритмы**: Falcon, Dilithium, Kyber - БЕЗОПАСНЫ
- ❌ **Классические алгоритмы**: ECDSA - УЯЗВИМЫ к квантовым атакам
- ❌ **НЕ РЕКОМЕНДУЕТСЯ** использовать классические алгоритмы в новых проектах
- ✅ **Совместимость**: Классические алгоритмы поддерживаются для легаси-систем

### **Рекомендации для новых проектов:**
🟢 **Используйте пост-квантовые алгоритмы:**
- **Falcon** (NIST finalist, решеточные подписи)
- **Dilithium** (NIST finalist, модулярные решетки)
- **Kyber** (NIST finalist, для обмена ключами)

🔴 **Избегайте классических алгоритмов:**
- **ECDSA** (уязвим к алгоритму Шора)

---

## Обзор

Модуль `dap_sign` предоставляет полный набор функций для работы с цифровыми подписями в DAP SDK. Поддерживает как классические алгоритмы (ECDSA для совместимости), так и современные пост-квантовые подписи (Dilithium, Falcon). Обеспечивает высокую безопасность и производительность для аутентификации данных в распределенных системах.

## Основные возможности

- **Множество алгоритмов подписей**: Классические и пост-квантовые
- **Гибкая система типов**: Поддержка различных форматов подписей
- **Автоматическая верификация**: Проверка подлинности подписей
- **Сериализация**: Сохранение и восстановление подписей
- **Интеграция с ключами**: Связь с системой управления ключами

## Архитектура

### Типы подписей

```c
typedef enum dap_sign_type_enum {
    // Базовые типы
    SIG_TYPE_NULL = 0x0000,           // Пустая подпись
    SIG_TYPE_BLISS = 0x0001,          // BLISS подпись
    SIG_TYPE_TESLA = 0x0003,          // TESLA подпись

    // Пост-квантовые подписи
    SIG_TYPE_PICNIC = 0x0101,         // Picnic ZKP-подпись
    SIG_TYPE_DILITHIUM = 0x0102,      // Dilithium (NIST finalist)
    SIG_TYPE_FALCON = 0x0103,         // Falcon (NIST finalist)
    SIG_TYPE_SPHINCSPLUS = 0x0104,    // Sphincs+ (NIST finalist)

    // Классические подписи
    SIG_TYPE_ECDSA = 0x0105,          // ECDSA
    SIG_TYPE_SHIPOVNIK = 0x0106,      // Shipovnik (Ring signatures)

    // Специализированные
    SIG_TYPE_MULTI_CHAINED = 0x0f00,  // Многоуровневые подписи
    SIG_TYPE_MULTI_COMBINED = 0x0f01, // Комбинированные подписи

    // PQLR интеграция (опционально)
    SIG_TYPE_PQLR_DILITHIUM = 0x1102, // PQLR Dilithium
    SIG_TYPE_PQLR_FALCON = 0x1103,    // PQLR Falcon
    SIG_TYPE_PQLR_SPHINCS = 0x1104    // PQLR Sphincs+
} dap_sign_type_enum_t;
```

### Типы хэширования для подписей

```c
#define DAP_SIGN_HASH_TYPE_NONE      0x00  // Без хэширования
#define DAP_SIGN_HASH_TYPE_SHA3      0x01  // SHA-3
#define DAP_SIGN_HASH_TYPE_STREEBOG  0x02  // Streebog (ГОСТ)
#define DAP_SIGN_HASH_TYPE_SIGN      0x0e  // Специфичное для подписи
#define DAP_SIGN_HASH_TYPE_DEFAULT   0x0f  // По умолчанию
```

### Структура заголовка подписи

```c
typedef struct dap_sign_hdr {
    dap_sign_type_t type;        // Тип подписи
    uint8_t hash_type;           // Тип хэширования
    uint8_t padding;             // Выравнивание
    uint32_t sign_size;          // Размер подписи
    uint32_t sign_pkey_size;     // Размер публичного ключа
} DAP_ALIGN_PACKED dap_sign_hdr_t;
```

### Структура подписи

```c
typedef struct dap_sign {
    dap_sign_hdr_t header;       // Заголовок подписи
    uint8_t pkey_n_sign[];       // Публичный ключ + подпись
} DAP_ALIGN_PACKED dap_sign_t;
```

## API Reference

### Инициализация

#### dap_sign_init()
```c
int dap_sign_init(uint8_t a_sign_hash_type_default);
```
**Описание**: Инициализирует систему цифровых подписей.

**Параметры**:
- `a_sign_hash_type_default` - тип хэширования по умолчанию

**Возвращает**:
- `0` - успех
- `-1` - ошибка инициализации

**Пример**:
```c
#include "dap_sign.h"

int main() {
    // Инициализация с SHA-3 по умолчанию
    if (dap_sign_init(DAP_SIGN_HASH_TYPE_SHA3) != 0) {
        fprintf(stderr, "Failed to initialize signature system\n");
        return 1;
    }

    // Работа с подписями...
    return 0;
}
```

### Создание подписей

#### dap_sign_create()
```c
DAP_STATIC_INLINE dap_sign_t *dap_sign_create(dap_enc_key_t *a_key,
                                             const void *a_data,
                                             const size_t a_data_size);
```
**Описание**: Создает цифровую подпись для данных.

**Параметры**:
- `a_key` - приватный ключ для подписи
- `a_data` - данные для подписи
- `a_data_size` - размер данных

**Возвращает**: Указатель на созданную подпись или `NULL` при ошибке

**Пример**:
```c
#include "dap_sign.h"
#include "dap_enc_key.h"

// Создание ключа для подписи
dap_enc_key_t *key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_DILITHIUM,
                                              NULL, 0, "seed", 4, 0);

const char *message = "Hello, World!";
dap_sign_t *signature = dap_sign_create(key, message, strlen(message));

if (signature) {
    printf("Signature created successfully\n");
    // Использование signature...
    free(signature);
} else {
    fprintf(stderr, "Failed to create signature\n");
}
```

#### dap_sign_create_with_hash_type()
```c
dap_sign_t *dap_sign_create_with_hash_type(dap_enc_key_t *a_key,
                                          const void *a_data,
                                          const size_t a_data_size,
                                          uint32_t a_hash_type);
```
**Описание**: Создает подпись с указанным типом хэширования.

**Пример**:
```c
// Создание подписи с Streebog хэшированием
dap_sign_t *signature = dap_sign_create_with_hash_type(key,
                                                      message, strlen(message),
                                                      DAP_SIGN_HASH_TYPE_STREEBOG);
```

### Верификация подписей

#### dap_sign_verify()
```c
DAP_STATIC_INLINE int dap_sign_verify(dap_sign_t *a_chain_sign,
                                     const void *a_data,
                                     const size_t a_data_size);
```
**Описание**: Проверяет цифровую подпись.

**Параметры**:
- `a_chain_sign` - подпись для проверки
- `a_data` - подписанные данные
- `a_data_size` - размер данных

**Возвращает**:
- `0` - подпись верна
- `-1` - подпись неверна
- `-2` - ошибка верификации

**Пример**:
```c
// Проверка подписи
int verify_result = dap_sign_verify(signature, message, strlen(message));

if (verify_result == 0) {
    printf("✅ Signature is valid\n");
} else if (verify_result == -1) {
    printf("❌ Signature is invalid\n");
} else {
    printf("⚠️  Signature verification error\n");
}
```

#### dap_sign_verify_by_pkey()
```c
int dap_sign_verify_by_pkey(dap_sign_t *a_chain_sign,
                           const void *a_data,
                           const size_t a_data_size,
                           dap_pkey_t *a_pkey);
```
**Описание**: Проверяет подпись с использованием указанного публичного ключа.

**Пример**:
```c
// Проверка с явным указанием публичного ключа
dap_pkey_t *public_key = extract_public_key(signature);
int result = dap_sign_verify_by_pkey(signature, message, strlen(message), public_key);
```

#### dap_sign_verify_size()
```c
DAP_STATIC_INLINE int dap_sign_verify_size(dap_sign_t *a_sign,
                                          size_t a_max_sign_size);
```
**Описание**: Проверяет корректность размера подписи.

**Пример**:
```c
// Проверка размера перед верификацией
if (dap_sign_verify_size(signature, MAX_SIGNATURE_SIZE) == 0) {
    // Размер корректный, можно проверять подпись
    dap_sign_verify(signature, data, data_size);
} else {
    fprintf(stderr, "Invalid signature size\n");
}
```

#### dap_sign_verify_all()
```c
DAP_STATIC_INLINE int dap_sign_verify_all(dap_sign_t *a_sign,
                                         const size_t a_sign_size_max,
                                         const void *a_data,
                                         const size_t a_data_size);
```
**Описание**: Полная проверка подписи (размер + валидность).

**Возвращает**:
- `0` - подпись верна
- `-1` - подпись неверна
- `-2` - некорректный размер

### Работа с компонентами подписи

#### dap_sign_get_sign()
```c
uint8_t *dap_sign_get_sign(dap_sign_t *a_sign, size_t *a_sign_size);
```
**Описание**: Извлекает данные подписи из структуры.

**Пример**:
```c
size_t sig_size;
uint8_t *signature_data = dap_sign_get_sign(signature, &sig_size);

if (signature_data) {
    printf("Signature size: %zu bytes\n", sig_size);
    // Работа с signature_data...
}
```

#### dap_sign_get_pkey()
```c
uint8_t *dap_sign_get_pkey(dap_sign_t *a_sign, size_t *a_pub_key_out);
```
**Описание**: Извлекает публичный ключ из подписи.

**Пример**:
```c
size_t pkey_size;
uint8_t *public_key = dap_sign_get_pkey(signature, &pkey_size);

if (public_key) {
    printf("Public key size: %zu bytes\n", pkey_size);
    // Работа с public_key...
}
```

#### dap_sign_get_pkey_hash()
```c
bool dap_sign_get_pkey_hash(dap_sign_t *a_sign, dap_chain_hash_fast_t *a_sign_hash);
```
**Описание**: Вычисляет хэш публичного ключа из подписи.

**Пример**:
```c
dap_chain_hash_fast_t pkey_hash;
if (dap_sign_get_pkey_hash(signature, &pkey_hash)) {
    char hash_str[DAP_CHAIN_HASH_FAST_STR_SIZE];
    dap_chain_hash_fast_to_str(&pkey_hash, hash_str, sizeof(hash_str));
    printf("Public key hash: %s\n", hash_str);
}
```

### Конвертация типов

#### dap_sign_type_from_key_type()
```c
dap_sign_type_t dap_sign_type_from_key_type(dap_enc_key_type_t a_key_type);
```
**Описание**: Конвертирует тип ключа в тип подписи.

**Пример**:
```c
dap_enc_key_type_t key_type = DAP_ENC_KEY_TYPE_SIG_DILITHIUM;
dap_sign_type_t sign_type = dap_sign_type_from_key_type(key_type);
// sign_type.type теперь содержит SIG_TYPE_DILITHIUM
```

#### dap_sign_type_to_key_type()
```c
dap_enc_key_type_t dap_sign_type_to_key_type(dap_sign_type_t a_chain_sign_type);
```
**Описание**: Конвертирует тип подписи в тип ключа.

### Строковые представления

#### dap_sign_type_to_str()
```c
const char *dap_sign_type_to_str(dap_sign_type_t a_chain_sign_type);
```
**Описание**: Конвертирует тип подписи в строку.

**Пример**:
```c
dap_sign_type_t sign_type = { .type = SIG_TYPE_DILITHIUM };
const char *type_str = dap_sign_type_to_str(sign_type);
printf("Signature type: %s\n", type_str); // "SIG_TYPE_DILITHIUM"
```

#### dap_sign_type_from_str()
```c
dap_sign_type_t dap_sign_type_from_str(const char *a_type_str);
```
**Описание**: Парсит тип подписи из строки.

**Пример**:
```c
dap_sign_type_t sign_type = dap_sign_type_from_str("SIG_TYPE_FALCON");
// sign_type.type теперь содержит SIG_TYPE_FALCON
```

### Работа с ключами

#### dap_sign_to_enc_key()
```c
DAP_STATIC_INLINE dap_enc_key_t *dap_sign_to_enc_key(dap_sign_t *a_chain_sign);
```
**Описание**: Создает объект ключа из подписи.

**Пример**:
```c
dap_enc_key_t *key = dap_sign_to_enc_key(signature);
if (key) {
    printf("Key type: %s\n", dap_enc_get_type_name(key->type));
    // Работа с key...
    dap_enc_key_delete(key);
}
```

#### dap_sign_to_enc_key_by_pkey()
```c
dap_enc_key_t *dap_sign_to_enc_key_by_pkey(dap_sign_t *a_chain_sign,
                                          dap_pkey_t *a_pkey);
```
**Описание**: Создает ключ из подписи с использованием внешнего публичного ключа.

### Информационные функции

#### dap_sign_get_information()
```c
void dap_sign_get_information(dap_sign_t *a_sign,
                             dap_string_t *a_str_out,
                             const char *a_hash_out_type);
```
**Описание**: Получает текстовую информацию о подписи.

**Пример**:
```c
dap_string_t info;
dap_sign_get_information(signature, &info, "hex");
printf("Signature info:\n%s\n", info.s);
dap_string_free(&info);
```

#### dap_sign_get_information_json()
```c
void dap_sign_get_information_json(json_object *a_json_arr_reply,
                                  dap_sign_t *a_sign,
                                  json_object *a_json_out,
                                  const char *a_hash_out_type,
                                  int a_version);
```
**Описание**: Получает информацию о подписи в формате JSON.

### Сравнение подписей

#### dap_sign_compare_pkeys()
```c
bool dap_sign_compare_pkeys(dap_sign_t *l_sign1, dap_sign_t *l_sign2);
```
**Описание**: Сравнивает публичные ключи двух подписей.

**Пример**:
```c
if (dap_sign_compare_pkeys(signature1, signature2)) {
    printf("Signatures have the same public key\n");
} else {
    printf("Signatures have different public keys\n");
}
```

### Утилитарные функции

#### dap_sign_get_size()
```c
uint64_t dap_sign_get_size(dap_sign_t *a_chain_sign);
```
**Описание**: Возвращает полный размер подписи в байтах.

#### dap_sign_type_is_depricated()
```c
bool dap_sign_type_is_depricated(dap_sign_type_t a_sign_type);
```
**Описание**: Проверяет, является ли тип подписи устаревшим.

#### dap_sign_get_str_recommended_types()
```c
const char *dap_sign_get_str_recommended_types();
```
**Описание**: Возвращает строку с рекомендуемыми типами подписей.

## Примеры использования

### Пример 1: Базовая подпись и верификация

```c
#include "dap_sign.h"
#include "dap_enc_key.h"

int basic_sign_verify_example() {
    // Инициализация
    if (dap_sign_init(DAP_SIGN_HASH_TYPE_SHA3) != 0) {
        return -1;
    }

    // Создание ключа для Dilithium подписи
    dap_enc_key_t *key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_DILITHIUM,
                                                  NULL, 0,
                                                  "secure_seed", 11,
                                                  0);

    if (!key) {
        fprintf(stderr, "Failed to generate key\n");
        return -1;
    }

    // Данные для подписи
    const char *message = "This is a test message for post-quantum signature";
    size_t message_len = strlen(message);

    // Создание подписи
    dap_sign_t *signature = dap_sign_create(key, message, message_len);

    if (!signature) {
        fprintf(stderr, "Failed to create signature\n");
        dap_enc_key_delete(key);
        return -1;
    }

    // Верификация подписи
    int verify_result = dap_sign_verify(signature, message, message_len);

    if (verify_result == 0) {
        printf("✅ Post-quantum signature verified successfully!\n");
        printf("Algorithm: Dilithium (NIST finalist)\n");
        printf("Security level: 128-bit against quantum attacks\n");
    } else {
        printf("❌ Signature verification failed\n");
    }

    // Очистка
    // Освобождение signature зависит от того, как она была создана
    dap_enc_key_delete(key);

    return verify_result == 0 ? 0 : -1;
}
```

### Пример 2: Работа с разными алгоритмами подписей

```c
#include "dap_sign.h"
#include "dap_enc_key.h"

typedef struct {
    dap_enc_key_type_t key_type;
    const char *description;
} signature_test_t;

int test_multiple_signatures() {
    signature_test_t tests[] = {
        {DAP_ENC_KEY_TYPE_SIG_DILITHIUM, "Dilithium (Post-quantum, NIST finalist)"},
        {DAP_ENC_KEY_TYPE_SIG_FALCON, "Falcon (Post-quantum, NIST finalist)"},
        {DAP_ENC_KEY_TYPE_SIG_ECDSA, "ECDSA (Classical, secp256r1)"},
        {DAP_ENC_KEY_TYPE_SIG_BLISS, "BLISS (Lattice-based)"}
    };

    const char *test_data = "Multi-signature test data";
    size_t data_size = strlen(test_data);

    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        printf("\n--- Testing %s ---\n", tests[i].description);

        // Создание ключа
        dap_enc_key_t *key = dap_enc_key_new_generate(tests[i].key_type,
                                                      NULL, 0,
                                                      "test_seed", 9,
                                                      0);

        if (!key) {
            printf("❌ Failed to generate key for %s\n", tests[i].description);
            continue;
        }

        // Создание подписи
        dap_sign_t *signature = dap_sign_create(key, test_data, data_size);

        if (!signature) {
            printf("❌ Failed to create signature for %s\n", tests[i].description);
            dap_enc_key_delete(key);
            continue;
        }

        // Верификация
        int verify_result = dap_sign_verify(signature, test_data, data_size);

        if (verify_result == 0) {
            // Получение информации о подписи
            dap_string_t info;
            dap_sign_get_information(signature, &info, "hex");

            printf("✅ %s signature successful\n", tests[i].description);
            printf("Signature info: %s\n", info.s);

            dap_string_free(&info);
        } else {
            printf("❌ %s signature verification failed\n", tests[i].description);
        }

        // Очистка
        dap_enc_key_delete(key);
        // signature нужно освободить в зависимости от реализации
    }

    return 0;
}
```

### Пример 3: Сериализация и хранение подписей

```c
#include "dap_sign.h"
#include "dap_enc_key.h"

int signature_storage_example() {
    // Создание подписи
    dap_enc_key_t *key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_DILITHIUM,
                                                  NULL, 0,
                                                  "storage_test", 12,
                                                  0);

    const char *data = "Data to be signed and stored";
    dap_sign_t *signature = dap_sign_create(key, data, strlen(data));

    if (!signature) {
        dap_enc_key_delete(key);
        return -1;
    }

    // Определение размера подписи
    uint64_t signature_size = dap_sign_get_size(signature);
    printf("Signature total size: %" PRIu64 " bytes\n", signature_size);

    // Сохранение подписи в файл
    FILE *file = fopen("signature.bin", "wb");
    if (file) {
        size_t written = fwrite(signature, 1, signature_size, file);
        fclose(file);

        if (written == signature_size) {
            printf("✅ Signature saved to file\n");
        } else {
            printf("❌ Failed to save signature\n");
        }
    }

    // Загрузка подписи из файла
    FILE *read_file = fopen("signature.bin", "rb");
    if (read_file) {
        // Определение размера файла
        fseek(read_file, 0, SEEK_END);
        size_t file_size = ftell(read_file);
        fseek(read_file, 0, SEEK_SET);

        // Чтение подписи
        dap_sign_t *loaded_signature = malloc(file_size);
        if (fread(loaded_signature, 1, file_size, read_file) == file_size) {
            fclose(read_file);

            // Верификация загруженной подписи
            int verify_result = dap_sign_verify(loaded_signature, data, strlen(data));

            if (verify_result == 0) {
                printf("✅ Loaded signature verified successfully\n");

                // Получение информации о подписи
                dap_string_t info;
                dap_sign_get_information(loaded_signature, &info, "hex");
                printf("Signature details:\n%s\n", info.s);
                dap_string_free(&info);

            } else {
                printf("❌ Loaded signature verification failed\n");
            }

            free(loaded_signature);
        } else {
            fclose(read_file);
            printf("❌ Failed to read signature from file\n");
        }
    }

    dap_enc_key_delete(key);
    return 0;
}
```

### Пример 4: Проверка подписи с дополнительными проверками

```c
#include "dap_sign.h"

int secure_signature_verification(dap_sign_t *signature,
                                 const void *data,
                                 size_t data_size,
                                 size_t max_signature_size) {
    if (!signature || !data || data_size == 0) {
        fprintf(stderr, "Invalid parameters\n");
        return -1;
    }

    // Шаг 1: Проверка размера подписи
    if (dap_sign_verify_size(signature, max_signature_size) != 0) {
        fprintf(stderr, "Signature size verification failed\n");
        return -2;
    }

    // Шаг 2: Проверка типа подписи
    if (signature->header.type.type == SIG_TYPE_NULL) {
        fprintf(stderr, "Null signature type not allowed\n");
        return -3;
    }

    // Шаг 3: Проверка на устаревший тип
    if (dap_sign_type_is_depricated(signature->header.type)) {
        fprintf(stderr, "Deprecated signature type detected\n");
        return -4;
    }

    // Шаг 4: Основная верификация
    int verify_result = dap_sign_verify(signature, data, data_size);

    if (verify_result == 0) {
        printf("✅ Signature verification successful\n");

        // Дополнительная информация
        const char *type_str = dap_sign_type_to_str(signature->header.type);
        printf("Signature type: %s\n", type_str);

        // Проверка использования хэша публичного ключа
        if (dap_sign_is_use_pkey_hash(signature)) {
            printf("Public key hash is used for verification\n");
        }

        return 0;
    } else if (verify_result == -1) {
        fprintf(stderr, "❌ Signature is invalid\n");
        return -5;
    } else {
        fprintf(stderr, "⚠️  Signature verification error\n");
        return -6;
    }
}

int comprehensive_verification_example() {
    // Создание подписи
    dap_enc_key_t *key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_FALCON,
                                                  NULL, 0,
                                                  "verify_test", 11,
                                                  0);

    const char *test_data = "Comprehensive verification test";
    dap_sign_t *signature = dap_sign_create(key, test_data, strlen(test_data));

    if (signature) {
        // Комплексная верификация
        int result = secure_signature_verification(signature,
                                                  test_data, strlen(test_data),
                                                  1024 * 1024); // 1MB max

        if (result == 0) {
            printf("🎉 All verification checks passed!\n");
        }

        dap_enc_key_delete(key);
        return result;
    }

    dap_enc_key_delete(key);
    return -1;
}
```

### Пример 5: Работа с многоуровневыми подписями

```c
#include "dap_sign.h"
#include "dap_enc_key.h"

int multi_signature_example() {
    // Создание нескольких ключей для многоуровневой подписи
    dap_enc_key_t *keys[3];

    keys[0] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_DILITHIUM, NULL, 0, "key1", 4, 0);
    keys[1] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_FALCON, NULL, 0, "key2", 4, 0);
    keys[2] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_ECDSA, NULL, 0, "key3", 4, 0);

    if (!keys[0] || !keys[1] || !keys[2]) {
        fprintf(stderr, "Failed to generate keys\n");
        // Очистка созданных ключей
        for (int i = 0; i < 3; i++) {
            if (keys[i]) dap_enc_key_delete(keys[i]);
        }
        return -1;
    }

    const char *data = "Multi-signature test data";
    size_t data_size = strlen(data);

    // Создание подписей каждым ключом
    dap_sign_t *signatures[3];
    for (int i = 0; i < 3; i++) {
        signatures[i] = dap_sign_create(keys[i], data, data_size);
        if (!signatures[i]) {
            fprintf(stderr, "Failed to create signature %d\n", i);
            // Очистка
            for (int j = 0; j < 3; j++) {
                if (keys[j]) dap_enc_key_delete(keys[j]);
                // signatures[j] нужно освободить
            }
            return -1;
        }
    }

    // Верификация каждой подписи
    const char *alg_names[] = {"Dilithium", "Falcon", "ECDSA"};

    for (int i = 0; i < 3; i++) {
        int verify_result = dap_sign_verify(signatures[i], data, data_size);

        if (verify_result == 0) {
            printf("✅ %s signature verified\n", alg_names[i]);

            // Получение размера подписи
            uint64_t sig_size = dap_sign_get_size(signatures[i]);
            printf("   Signature size: %" PRIu64 " bytes\n", sig_size);

        } else {
            printf("❌ %s signature verification failed\n", alg_names[i]);
        }
    }

    printf("\n📊 Multi-signature Summary:\n");
    printf("- Total signatures: 3\n");
    printf("- Algorithms: Dilithium (PQ), Falcon (PQ), ECDSA (Classical)\n");
    printf("- Combined security: Post-quantum + Classical protection\n");

    // Очистка
    for (int i = 0; i < 3; i++) {
        dap_enc_key_delete(keys[i]);
        // signatures[i] нужно освободить в зависимости от реализации
    }

    return 0;
}
```

## Производительность

### Время создания подписей

| Алгоритм | Время подписи | Время верификации | Размер подписи |
|----------|----------------|-------------------|----------------|
| Dilithium | ~50 μs | ~25 μs | 2,420 bytes |
| Falcon | ~100 μs | ~15 μs | 690 bytes |
| ECDSA | ~20 μs | ~40 μs | 64 bytes |
| BLISS | ~30 μs | ~20 μs | 1,200 bytes |

### Время верификации подписей

| Алгоритм | Время верификации | CPU нагрузка |
|----------|-------------------|--------------|
| Dilithium | ~25 μs | Средняя |
| Falcon | ~15 μs | Низкая |
| ECDSA | ~40 μs | Низкая |
| BLISS | ~20 μs | Средняя |

## Безопасность

### Уровни безопасности алгоритмов

#### Пост-квантовые алгоритмы (рекомендуются):
- **Dilithium**: 128/192/256-bit безопасность против квантовых атак
- **Falcon**: 128/192/256-bit безопасность
- **Sphincs+**: 128/192/256-bit безопасность (stateful)

#### Классические алгоритмы:
- **ECDSA**: 128-bit безопасность (уязвим к квантовым атакам)
- **BLISS**: 128-bit безопасность (Lattice-based)
- **TESLA**: Специализированная безопасность

### Рекомендации по выбору алгоритма

#### Для новых систем (рекомендуется):
```c
// Пост-квантовые алгоритмы (будущая безопасность)
dap_enc_key_t *key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_DILITHIUM, ...);
// или
dap_enc_key_t *key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_FALCON, ...);
```

#### Для производительности:
```c
// Быстрые алгоритмы
dap_enc_key_t *key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_FALCON, ...); // Самый быстрый
// или
dap_enc_key_t *key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_BLISS, ...);
```

#### Для совместимости:
```c
// Классические алгоритмы
dap_enc_key_t *key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_ECDSA, ...);
```

### Многоуровневая защита

```c
// Создание многоуровневой подписи
void create_multi_level_signature(const void *data, size_t data_size) {
    // Уровень 1: Пост-квантовая подпись (защита от квантовых атак)
    dap_enc_key_t *pq_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_DILITHIUM, ...);
    dap_sign_t *pq_signature = dap_sign_create(pq_key, data, data_size);

    // Уровень 2: Классическая подпись (текущая совместимость)
    dap_enc_key_t *classic_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_ECDSA, ...);
    dap_sign_t *classic_signature = dap_sign_create(classic_key, data, data_size);

    // Сохранение обеих подписей
    // ...
}
```

## Лучшие практики

### 1. Выбор алгоритма подписи

```c
// Правильный выбор алгоритма
dap_enc_key_type_t select_signature_algorithm(bool need_quantum_resistance,
                                            bool need_high_speed,
                                            bool need_compact_size) {
    if (need_quantum_resistance) {
        if (need_high_speed) {
            return DAP_ENC_KEY_TYPE_SIG_FALCON;  // Быстрый + PQ
        } else if (need_compact_size) {
            return DAP_ENC_KEY_TYPE_SIG_FALCON;  // Компактный + PQ
        } else {
            return DAP_ENC_KEY_TYPE_SIG_DILITHIUM; // Стандарт PQ
        }
    } else {
        if (need_high_speed) {
            return DAP_ENC_KEY_TYPE_SIG_ECDSA;   // Быстрый классический
        } else {
            return DAP_ENC_KEY_TYPE_SIG_BLISS;   // Lattice-based
        }
    }
}
```

### 2. Безопасная верификация

```c
// Комплексная проверка подписи
int secure_verify_signature(dap_sign_t *signature,
                           const void *data, size_t data_size,
                           size_t max_size) {
    // Проверка входных параметров
    if (!signature || !data || data_size == 0) {
        return -1;
    }

    // Проверка размера
    if (dap_sign_verify_size(signature, max_size) != 0) {
        return -2;
    }

    // Проверка типа
    if (signature->header.type.type == SIG_TYPE_NULL) {
        return -3;
    }

    // Основная верификация
    return dap_sign_verify(signature, data, data_size);
}
```

### 3. Управление ключами подписи

```c
// Правильное управление ключами
typedef struct signature_context {
    dap_enc_key_t *signing_key;
    dap_enc_key_t *verification_key;
    time_t key_created;
    time_t key_expires;
} signature_context_t;

signature_context_t *create_signature_context(dap_enc_key_type_t key_type) {
    signature_context_t *ctx = calloc(1, sizeof(signature_context_t));

    if (!ctx) return NULL;

    // Создание ключа для подписи
    ctx->signing_key = dap_enc_key_new_generate(key_type,
                                               NULL, 0,
                                               "signing_key_seed", 15,
                                               0);

    if (!ctx->signing_key) {
        free(ctx);
        return NULL;
    }

    // Создание ключа для верификации (с тем же публичным ключом)
    ctx->verification_key = dap_sign_to_enc_key(ctx->signing_key);

    ctx->key_created = time(NULL);
    ctx->key_expires = ctx->key_created + (365 * 24 * 60 * 60); // 1 год

    return ctx;
}

void destroy_signature_context(signature_context_t *ctx) {
    if (ctx) {
        if (ctx->signing_key) dap_enc_key_delete(ctx->signing_key);
        if (ctx->verification_key) dap_enc_key_delete(ctx->verification_key);
        free(ctx);
    }
}
```

### 4. Обработка ошибок

```c
// Надежная обработка ошибок при работе с подписями
dap_sign_t *create_signature_safe(dap_enc_key_t *key,
                                 const void *data, size_t data_size,
                                 const char **error_msg) {
    // Проверка параметров
    if (!key || !data || data_size == 0) {
        *error_msg = "Invalid parameters";
        return NULL;
    }

    // Проверка типа ключа
    if (key->type < DAP_ENC_KEY_TYPE_SIG_BLISS ||
        key->type > DAP_ENC_KEY_TYPE_SIG_SHIPOVNIK) {
        *error_msg = "Key is not suitable for signing";
        return NULL;
    }

    // Создание подписи
    dap_sign_t *signature = dap_sign_create(key, data, data_size);

    if (!signature) {
        *error_msg = "Failed to create signature";
        return NULL;
    }

    // Проверка созданной подписи
    if (dap_sign_verify_size(signature, 1024 * 1024) != 0) {
        *error_msg = "Created signature has invalid size";
        // signature нужно освободить
        return NULL;
    }

    *error_msg = NULL;
    return signature;
}
```

## Заключение

Модуль `dap_sign` предоставляет мощную и гибкую систему цифровых подписей:

### Ключевые преимущества:
- **Пост-квантовая безопасность**: Поддержка современных алгоритмов
- **Высокая производительность**: Оптимизированные реализации
- **Гибкость**: Множество алгоритмов на выбор
- **Надежность**: Комплексная система верификации

### Основные возможности:
- Поддержка 10+ алгоритмов подписей
- Пост-квантовая криптография (Dilithium, Falcon, Sphincs+)
- Классические алгоритмы (ECDSA, BLISS)
- Специализированные подписи (Ring signatures, Multisign)
- Полная сериализация и десериализация

### Рекомендации по использованию:
1. **Для новых проектов**: Используйте пост-квантовые алгоритмы (Dilithium/Falcon)
2. **Для скорости**: Выбирайте Falcon (самый быстрый)
3. **Для размера**: Учитывайте размер подписей (ECDSA - самый компактный)
4. **Всегда проверяйте** результаты операций с подписями
5. **Используйте комплексную верификацию** (размер + валидность + тип)

### Следующие шаги:
1. Изучите конкретные алгоритмы в разделах документации
2. Ознакомьтесь с примерами использования
3. Выберите подходящий алгоритм для вашего проекта
4. Реализуйте систему подписей в вашем приложении
5. Следите за обновлениями криптографических стандартов

Для получения дополнительной информации смотрите:
- `dap_sign.h` - полный API подписей
- `dap_enc_key.h` - управление ключами
- `dap_hash.h` - хэш-функции
- Примеры в директории `examples/crypto/`
- Тесты в директории `test/crypto/`
