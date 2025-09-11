# dap_enc_key.h - Управление криптографическими ключами

## Обзор

Модуль `dap_enc_key` предоставляет унифицированный интерфейс для работы со всеми типами криптографических ключей в DAP SDK. Он поддерживает как классические, так и пост-квантовые криптографические алгоритмы через единый API.

## Основные возможности

- **Унифицированный API**: Единый интерфейс для всех типов ключей
- **Автоматическое управление**: Управление памятью и ресурсами
- **Сериализация**: Сохранение и восстановление ключей
- **Многообразие алгоритмов**: Поддержка 20+ криптографических алгоритмов
- **Кроссплатформенность**: Работа на всех поддерживаемых платформах

## Архитектура

### Структура dap_enc_key_t

```c
typedef struct dap_enc_key {
    // Размер приватного ключа/общего секрета
    union {
        size_t priv_key_data_size;
        size_t shared_key_size;
    };

    // Данные приватного ключа/общего секрета
    union {
        void *priv_key_data;
        byte_t *shared_key;
    };

    // Размер публичного ключа
    size_t pub_key_data_size;
    void *pub_key_data;

    // Метка времени последнего использования
    time_t last_used_timestamp;

    // Тип криптографического алгоритма
    dap_enc_key_type_t type;

    // Функции шифрования/дешифрования
    dap_enc_callback_dataop_t enc;
    dap_enc_callback_dataop_t dec;
    dap_enc_callback_dataop_na_t enc_na;
    dap_enc_callback_dataop_na_t dec_na;
    dap_enc_callback_dataop_na_ext_t dec_na_ext;

    // Функции цифровой подписи
    dap_enc_callback_sign_op_t sign_get;
    dap_enc_callback_sign_op_t sign_verify;

    // Функции обмена ключами (для асимметричных алгоритмов)
    dap_enc_gen_bob_shared_key gen_bob_shared_key;
    dap_enc_gen_alice_shared_key gen_alice_shared_key;

    // Дополнительные поля для специализированных алгоритмов
    void *pbk_list_data;
    size_t pbk_list_size;
    dap_enc_get_allpbk_list get_all_pbk_list;

    // Указатель на специфичные данные алгоритма
    void *_pvt;
    void *_inheritor;
    size_t _inheritor_size;

} dap_enc_key_t;
```

## Типы криптографических алгоритмов

### Симметричные алгоритмы шифрования

```c
// AES шифрование
DAP_ENC_KEY_TYPE_IAES = 0              // Стандартный AES
DAP_ENC_KEY_TYPE_OAES = 1              // OAES (из Monero)

// Blowfish
DAP_ENC_KEY_TYPE_BF_CBC = 2            // Blowfish CBC режим
DAP_ENC_KEY_TYPE_BF_OFB = 3            // Blowfish OFB режим

// ГОСТ алгоритмы
DAP_ENC_KEY_TYPE_GOST_OFB = 4          // ГОСТ 28147-89 OFB
DAP_ENC_KEY_TYPE_KUZN_OFB = 5          // ГОСТ 28147-14 OFB

// Потоковые шифры
DAP_ENC_KEY_TYPE_SALSA2012 = 6         // Salsa2012
DAP_ENC_KEY_TYPE_SEED_OFB = 7          // SEED OFB
```

### Пост-квантовые алгоритмы

```c
// Протоколы обмена ключами (KEM)
DAP_ENC_KEY_TYPE_RLWE_NEWHOPE_CPA_KEM = 8    // NewHope CPA-KEM
DAP_ENC_KEY_TYPE_MSRLN = 11                  // Microsoft Research Ring-LWE
DAP_ENC_KEY_TYPE_RLWE_MSRLN16 = 12           // MSR-LN16
DAP_ENC_KEY_TYPE_RLWE_BCNS15 = 13            // BCNS15
DAP_ENC_KEY_TYPE_LWE_FRODO = 14              // Frodo
DAP_ENC_KEY_TYPE_CODE_MCBITS = 15            // McBits
DAP_ENC_KEY_TYPE_NTRU = 16                   // NTRU
DAP_ENC_KEY_TYPE_MLWE_KYBER = 17             // Kyber (NIST finalist)

// Подписи
DAP_ENC_KEY_TYPE_SIG_PICNIC = 18             // Picnic
DAP_ENC_KEY_TYPE_SIG_BLISS = 19              // BLISS
DAP_ENC_KEY_TYPE_SIG_TESLA = 20              // TESLA
DAP_ENC_KEY_TYPE_SIG_DILITHIUM = 21          // Dilithium (NIST finalist)
DAP_ENC_KEY_TYPE_SIG_RINGCT20 = 22           // RingCT20
DAP_ENC_KEY_TYPE_KEM_KYBER512 = 23           // Kyber KEM
DAP_ENC_KEY_TYPE_SIG_FALCON = 24             // Falcon (NIST finalist)
DAP_ENC_KEY_TYPE_SIG_SPHINCSPLUS = 25        // Sphincs+ (NIST finalist)
```

### Классические алгоритмы

```c
// Цифровые подписи
DAP_ENC_KEY_TYPE_SIG_ECDSA = 26              // ECDSA
DAP_ENC_KEY_TYPE_SIG_SHIPOVNIK = 27          // Shipovnik (Ring signatures)

// Многосторонние подписи
DAP_ENC_KEY_TYPE_SIG_MULTI_CHAINED = 100     // Multisign
```

## API Reference

### Инициализация

#### dap_enc_key_init()
```c
int dap_enc_key_init(void);
```
**Описание**: Инициализирует систему управления ключами.

**Возвращает**:
- `0` - успех
- `-1` - ошибка инициализации

#### dap_enc_key_deinit()
```c
void dap_enc_key_deinit(void);
```
**Описание**: Завершает работу системы управления ключами.

### Создание ключей

#### dap_enc_key_new()
```c
dap_enc_key_t *dap_enc_key_new(dap_enc_key_type_t a_key_type);
```
**Описание**: Создает новый объект ключа заданного типа.

**Параметры**:
- `a_key_type` - тип криптографического алгоритма

**Возвращает**: Указатель на созданный ключ или `NULL` при ошибке

**Пример**:
```c
#include "dap_enc_key.h"

// Создание AES ключа
dap_enc_key_t *aes_key = dap_enc_key_new(DAP_ENC_KEY_TYPE_IAES);
if (!aes_key) {
    fprintf(stderr, "Failed to create AES key\n");
    return NULL;
}
```

#### dap_enc_key_new_generate()
```c
dap_enc_key_t *dap_enc_key_new_generate(dap_enc_key_type_t a_key_type,
                                        const void *a_kex_buf, size_t a_kex_size,
                                        const void *a_seed, size_t a_seed_size,
                                        size_t a_key_size);
```
**Описание**: Создает и генерирует новый ключ с заданными параметрами.

**Параметры**:
- `a_key_type` - тип алгоритма
- `a_kex_buf` - буфер для key exchange (может быть NULL)
- `a_kex_size` - размер key exchange буфера
- `a_seed` - seed для генерации ключа
- `a_seed_size` - размер seed
- `a_key_size` - требуемый размер ключа

**Возвращает**: Сгенерированный ключ или `NULL` при ошибке

**Пример**:
```c
// Генерация Dilithium ключа для подписей
dap_enc_key_t *sign_key = dap_enc_key_new_generate(
    DAP_ENC_KEY_TYPE_SIG_DILITHIUM,
    NULL, 0,                    // без KEX
    "my_secure_seed", 14,       // seed для генерации
    0                          // размер по умолчанию
);

if (!sign_key) {
    fprintf(stderr, "Failed to generate Dilithium key\n");
}
```

### Сериализация ключей

#### dap_enc_key_serialize()
```c
uint8_t *dap_enc_key_serialize(dap_enc_key_t *a_key, size_t *a_buflen);
```
**Описание**: Сериализует ключ в бинарный формат для хранения или передачи.

**Параметры**:
- `a_key` - ключ для сериализации
- `a_buflen` - указатель для сохранения размера сериализованных данных

**Возвращает**: Сериализованные данные или `NULL` при ошибке

**Пример**:
```c
size_t serialized_size;
uint8_t *serialized_data = dap_enc_key_serialize(key, &serialized_size);

if (serialized_data) {
    // Сохранить serialized_data в файл или передать по сети
    printf("Key serialized: %zu bytes\n", serialized_size);

    // Сохранить в файл
    FILE *file = fopen("key.bin", "wb");
    if (file) {
        fwrite(serialized_data, 1, serialized_size, file);
        fclose(file);
    }

    free(serialized_data);
}
```

#### dap_enc_key_deserialize()
```c
dap_enc_key_t *dap_enc_key_deserialize(const void *buf, size_t a_buf_size);
```
**Описание**: Восстанавливает ключ из сериализованного бинарного формата.

**Параметры**:
- `buf` - сериализованные данные
- `a_buf_size` - размер данных

**Возвращает**: Восстановленный ключ или `NULL` при ошибке

**Пример**:
```c
// Загрузка ключа из файла
FILE *file = fopen("key.bin", "rb");
if (file) {
    fseek(file, 0, SEEK_END);
    size_t file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    uint8_t *buffer = malloc(file_size);
    if (fread(buffer, 1, file_size, file) == file_size) {
        dap_enc_key_t *loaded_key = dap_enc_key_deserialize(buffer, file_size);
        if (loaded_key) {
            printf("Key loaded successfully\n");
            // Использовать loaded_key
            dap_enc_key_delete(loaded_key);
        }
    }
    free(buffer);
    fclose(file);
}
```

### Сериализация публичных и приватных ключей

#### dap_enc_key_serialize_pub_key()
```c
uint8_t *dap_enc_key_serialize_pub_key(dap_enc_key_t *a_key, size_t *a_buflen_out);
```
**Описание**: Сериализует только публичную часть ключа.

#### dap_enc_key_serialize_priv_key()
```c
uint8_t *dap_enc_key_serialize_priv_key(dap_enc_key_t *a_key, size_t *a_buflen_out);
```
**Описание**: Сериализует только приватную часть ключа.

#### dap_enc_key_deserialize_pub_key()
```c
int dap_enc_key_deserialize_pub_key(dap_enc_key_t *a_key, const uint8_t *a_buf, size_t a_buflen);
```
**Описание**: Загружает публичную часть ключа.

#### dap_enc_key_deserialize_priv_key()
```c
int dap_enc_key_deserialize_priv_key(dap_enc_key_t *a_key, const uint8_t *a_buf, size_t a_buflen);
```
**Описание**: Загружает приватную часть ключа.

### Работа с подписями

#### dap_enc_key_sign_get()
```c
size_t dap_enc_key_sign_get(dap_enc_key_t *a_key,
                           const void *a_data, size_t a_data_size,
                           void *a_sig, size_t a_sig_size_max);
```
**Описание**: Создает цифровую подпись для данных.

**Пример**:
```c
const char *message = "Hello, World!";
size_t message_len = strlen(message);

// Вычисление размера подписи
size_t sig_size = dap_enc_calc_signature_unserialized_size(a_key);
uint8_t *signature = malloc(sig_size);

if (signature) {
    size_t actual_sig_size = a_key->sign_get(a_key,
                                           message, message_len,
                                           signature, sig_size);

    if (actual_sig_size > 0) {
        printf("Signature created: %zu bytes\n", actual_sig_size);
        // Использовать signature
    }
    free(signature);
}
```

#### dap_enc_key_sign_verify()
```c
int dap_enc_key_sign_verify(dap_enc_key_t *a_key,
                           const void *a_data, size_t a_data_size,
                           const void *a_sig, size_t a_sig_size);
```
**Описание**: Проверяет цифровую подпись.

**Возвращает**:
- `0` - подпись верна
- `-1` - подпись неверна
- `-2` - ошибка проверки

**Пример**:
```c
int verify_result = a_key->sign_verify(a_key,
                                     message, message_len,
                                     signature, sig_size);

if (verify_result == 0) {
    printf("✅ Signature is valid\n");
} else if (verify_result == -1) {
    printf("❌ Signature is invalid\n");
} else {
    printf("⚠️  Signature verification error\n");
}
```

### Обмен ключами (KEM)

#### gen_bob_shared_key()
```c
size_t gen_bob_shared_key(dap_enc_key_t *b_key, const void *a_pub,
                         size_t a_pub_size, void **b_pub);
```
**Описание**: Генерирует публичный ключ для Боба и вычисляет общий секрет.

**Пример**:
```c
// Пример с Kyber KEM
dap_enc_key_t *alice_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_MLWE_KYBER, ...);
dap_enc_key_t *bob_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_MLWE_KYBER, ...);

// Алиса генерирует публичный ключ
void *alice_public = NULL;
size_t alice_pub_size = alice_key->gen_bob_shared_key(alice_key, NULL, 0, &alice_public);

// Боб получает публичный ключ Алисы и генерирует общий секрет
size_t bob_shared_size = bob_key->gen_alice_shared_key(bob_key, NULL, alice_pub_size, alice_public);

// Теперь у Алисы и Боба есть одинаковый общий секрет
// alice_key->priv_key_data и bob_key->priv_key_data содержат общий секрет

free(alice_public);
```

### Вспомогательные функции

#### dap_enc_get_type_name()
```c
const char *dap_enc_get_type_name(dap_enc_key_type_t a_key_type);
```
**Описание**: Возвращает строковое имя типа ключа.

**Пример**:
```c
dap_enc_key_type_t key_type = DAP_ENC_KEY_TYPE_SIG_DILITHIUM;
const char *type_name = dap_enc_get_type_name(key_type);
printf("Key type: %s\n", type_name); // Вывод: SIG_DILITHIUM
```

#### dap_enc_key_type_find_by_name()
```c
dap_enc_key_type_t dap_enc_key_type_find_by_name(const char *a_name);
```
**Описание**: Находит тип ключа по строковому имени.

#### dap_enc_calc_signature_unserialized_size()
```c
size_t dap_enc_calc_signature_unserialized_size(dap_enc_key_t *a_key);
```
**Описание**: Вычисляет размер подписи для данного ключа.

### Удаление ключей

#### dap_enc_key_delete()
```c
void dap_enc_key_delete(dap_enc_key_t *a_key);
```
**Описание**: Удаляет ключ и освобождает все связанные ресурсы.

**Пример**:
```c
// Правильная очистка ключа
if (key) {
    dap_enc_key_delete(key);
    key = NULL;
}
```

## Примеры использования

### Пример 1: Работа с ECDSA подписями

```c
#include "dap_enc.h"
#include "dap_enc_key.h"
#include <string.h>

int ecdsa_sign_verify_example() {
    // Инициализация
    if (dap_enc_key_init() != 0) {
        return -1;
    }

    // Создание ECDSA ключа
    dap_enc_key_t *key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_ECDSA,
                                                  NULL, 0,
                                                  "ecdsa_seed", 10,
                                                  256); // secp256r1

    if (!key) {
        fprintf(stderr, "Failed to generate ECDSA key\n");
        return -1;
    }

    // Данные для подписи
    const char *message = "ECDSA signature test";
    size_t message_len = strlen(message);

    // Вычисление размера подписи
    size_t sig_size = dap_enc_calc_signature_unserialized_size(key);
    uint8_t *signature = malloc(sig_size);

    if (!signature) {
        dap_enc_key_delete(key);
        return -1;
    }

    // Создание подписи
    size_t actual_sig_size = key->sign_get(key, message, message_len,
                                         signature, sig_size);

    if (actual_sig_size == 0) {
        fprintf(stderr, "Failed to create signature\n");
        free(signature);
        dap_enc_key_delete(key);
        return -1;
    }

    // Проверка подписи
    int verify_result = key->sign_verify(key, message, message_len,
                                       signature, actual_sig_size);

    if (verify_result == 0) {
        printf("✅ ECDSA signature verified successfully\n");
    } else {
        printf("❌ ECDSA signature verification failed\n");
    }

    // Очистка
    free(signature);
    dap_enc_key_delete(key);
    dap_enc_key_deinit();

    return verify_result == 0 ? 0 : -1;
}
```

### Пример 2: Пост-квантовый KEM с Kyber

```c
#include "dap_enc.h"
#include "dap_enc_key.h"

int kyber_kem_example() {
    // Инициализация
    if (dap_enc_key_init() != 0) {
        return -1;
    }

    // Создание ключей для Алисы и Боба
    dap_enc_key_t *alice_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_MLWE_KYBER,
                                                        NULL, 0,
                                                        "alice_seed", 10,
                                                        0);

    dap_enc_key_t *bob_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_MLWE_KYBER,
                                                      NULL, 0,
                                                      "bob_seed", 8,
                                                      0);

    if (!alice_key || !bob_key) {
        fprintf(stderr, "Failed to generate Kyber keys\n");
        goto cleanup;
    }

    // Алиса генерирует публичный ключ
    void *alice_public = NULL;
    size_t alice_pub_size = alice_key->gen_bob_shared_key(alice_key,
                                                        NULL, 0,
                                                        &alice_public);

    if (alice_pub_size == 0) {
        fprintf(stderr, "Alice failed to generate public key\n");
        goto cleanup;
    }

    printf("Alice public key size: %zu bytes\n", alice_pub_size);

    // Боб получает публичный ключ Алисы и генерирует общий секрет
    size_t bob_shared_size = bob_key->gen_alice_shared_key(bob_key,
                                                         bob_key->priv_key_data,
                                                         alice_pub_size,
                                                         alice_public);

    if (bob_shared_size == 0) {
        fprintf(stderr, "Bob failed to generate shared secret\n");
        free(alice_public);
        goto cleanup;
    }

    // Алиса тоже генерирует общий секрет
    size_t alice_shared_size = alice_key->gen_alice_shared_key(alice_key,
                                                             alice_key->priv_key_data,
                                                             alice_key->pub_key_data_size,
                                                             alice_key->pub_key_data);

    // Проверка, что общие секреты совпадают
    if (alice_shared_size == bob_shared_size &&
        memcmp(alice_key->priv_key_data, bob_key->priv_key_data,
               alice_shared_size) == 0) {
        printf("✅ Kyber KEM successful! Shared secret: %zu bytes\n",
               alice_shared_size);
    } else {
        printf("❌ Kyber KEM failed - secrets don't match\n");
    }

    free(alice_public);

cleanup:
    if (alice_key) dap_enc_key_delete(alice_key);
    if (bob_key) dap_enc_key_delete(bob_key);

    dap_enc_key_deinit();
    return 0;
}
```

### Пример 3: Сериализация и хранение ключей

```c
#include "dap_enc.h"
#include "dap_enc_key.h"

int key_storage_example() {
    // Создание ключа
    dap_enc_key_t *key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_DILITHIUM,
                                                  NULL, 0,
                                                  "storage_test", 12,
                                                  0);

    if (!key) {
        return -1;
    }

    // Сериализация приватного ключа
    size_t priv_key_size;
    uint8_t *priv_key_data = dap_enc_key_serialize_priv_key(key, &priv_key_size);

    if (priv_key_data) {
        // Сохранение приватного ключа в защищенное хранилище
        FILE *priv_file = fopen("private_key.bin", "wb");
        if (priv_file) {
            fwrite(priv_key_data, 1, priv_key_size, priv_file);
            fclose(priv_file);
            printf("Private key saved: %zu bytes\n", priv_key_size);
        }
        free(priv_key_data);
    }

    // Сериализация публичного ключа
    size_t pub_key_size;
    uint8_t *pub_key_data = dap_enc_key_serialize_pub_key(key, &pub_key_size);

    if (pub_key_data) {
        // Публичный ключ можно хранить открыто
        FILE *pub_file = fopen("public_key.bin", "wb");
        if (pub_file) {
            fwrite(pub_key_data, 1, pub_key_size, pub_file);
            fclose(pub_file);
            printf("Public key saved: %zu bytes\n", pub_key_size);
        }
        free(pub_key_data);
    }

    // Полная сериализация ключа
    size_t full_key_size;
    uint8_t *full_key_data = dap_enc_key_serialize(key, &full_key_size);

    if (full_key_data) {
        FILE *full_file = fopen("full_key.bin", "wb");
        if (full_file) {
            fwrite(full_key_data, 1, full_key_size, full_file);
            fclose(full_file);
            printf("Full key saved: %zu bytes\n", full_key_size);
        }
        free(full_key_data);
    }

    dap_enc_key_delete(key);
    return 0;
}
```

### Пример 4: Загрузка ключа из хранилища

```c
dap_enc_key_t *load_key_from_file(const char *filename) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        return NULL;
    }

    // Определение размера файла
    fseek(file, 0, SEEK_END);
    size_t file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // Чтение файла
    uint8_t *buffer = malloc(file_size);
    if (!buffer) {
        fclose(file);
        return NULL;
    }

    if (fread(buffer, 1, file_size, file) != file_size) {
        free(buffer);
        fclose(file);
        return NULL;
    }

    fclose(file);

    // Десериализация ключа
    dap_enc_key_t *key = dap_enc_key_deserialize(buffer, file_size);
    free(buffer);

    if (key) {
        printf("Key loaded successfully from %s\n", filename);
        printf("Key type: %s\n", dap_enc_get_type_name(key->type));
    }

    return key;
}

int load_and_use_key_example() {
    // Загрузка публичного ключа
    dap_enc_key_t *pub_key = load_key_from_file("public_key.bin");
    if (!pub_key) {
        return -1;
    }

    // Загрузка приватного ключа (только для владельца)
    dap_enc_key_t *priv_key = load_key_from_file("private_key.bin");
    if (!priv_key) {
        dap_enc_key_delete(pub_key);
        return -1;
    }

    // Использование ключей для подписи и проверки
    const char *message = "Test message for signature";
    size_t message_len = strlen(message);

    // Создание подписи приватным ключом
    size_t sig_size = dap_enc_calc_signature_unserialized_size(priv_key);
    uint8_t *signature = malloc(sig_size);

    if (signature) {
        size_t actual_sig_size = priv_key->sign_get(priv_key,
                                                   message, message_len,
                                                   signature, sig_size);

        if (actual_sig_size > 0) {
            // Проверка подписи публичным ключом
            int verify_result = pub_key->sign_verify(pub_key,
                                                   message, message_len,
                                                   signature, actual_sig_size);

            if (verify_result == 0) {
                printf("✅ Signature verified with loaded keys\n");
            }
        }
        free(signature);
    }

    dap_enc_key_delete(pub_key);
    dap_enc_key_delete(priv_key);

    return 0;
}
```

## Производительность

### Время генерации ключей

| Алгоритм | Время генерации | Безопасность |
|----------|----------------|-------------|
| AES-256 | ~1 μs | 128-bit |
| ECDSA | ~10 μs | 128-bit |
| Kyber | ~50 μs | 128-bit PQ |
| Dilithium | ~100 μs | 128-bit PQ |
| Falcon | ~200 μs | 128-bit PQ |

### Время операций подписи

| Алгоритм | Подпись | Проверка | Размер подписи |
|----------|---------|----------|----------------|
| ECDSA | ~20 μs | ~40 μs | 64 bytes |
| Dilithium | ~50 μs | ~25 μs | 2,420 bytes |
| Falcon | ~100 μs | ~15 μs | 690 bytes |

## Безопасность

### Рекомендации по использованию

#### Для новых проектов:
```c
// Пост-квантовые алгоритмы (рекомендуется)
dap_enc_key_t *sign_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_DILITHIUM, ...);
dap_enc_key_t *kem_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_MLWE_KYBER, ...);
```

#### Для совместимости:
```c
// Классические алгоритмы (только если требуется совместимость)
dap_enc_key_t *legacy_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_ECDSA, ...);
```

### Управление ключами

1. **Хранение**: Приватные ключи должны храниться в защищенном хранилище
2. **Передача**: Публичные ключи можно передавать открыто
3. **Обновление**: Регулярно обновляйте ключи для поддержания безопасности
4. **Резервное копирование**: Создавайте резервные копии важных ключей

### Предупреждения

- Никогда не храните приватные ключи в открытом виде
- Используйте сильные seed для генерации ключей
- Регулярно проверяйте целостность ключей
- Избегайте повторного использования ключей для разных целей

## Заключение

Модуль `dap_enc_key` предоставляет мощный и гибкий интерфейс для работы со всеми типами криптографических ключей:

### Ключевые преимущества:
- **Единый API** для всех алгоритмов
- **Поддержка пост-квантовой криптографии**
- **Автоматическое управление ресурсами**
- **Кроссплатформенность**
- **Высокая производительность**

### Основные возможности:
- Генерация и управление ключами
- Сериализация/десериализация
- Цифровые подписи
- Обмен ключами (KEM)
- Поддержка множественных алгоритмов

Для получения дополнительной информации смотрите:
- `dap_enc_key.h` - полный API ключей
- `dap_enc.h` - основной крипто-API
- Примеры в директории `examples/crypto/`
- Тесты в директории `test/crypto/`

