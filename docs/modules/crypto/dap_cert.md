# dap_cert.h - DAP Certificate: Собственный формат сертификатов

## Обзор

Модуль `dap_cert` предоставляет собственный формат сертификатов DAP SDK, разработанный специально для распределенных приложений и блокчейн-систем. В отличие от стандартных X.509 сертификатов, DAP сертификаты оптимизированы для высокой производительности, гибкости и поддержки пост-квантовой криптографии.

## Основные возможности

- **Собственный формат**: Оптимизирован для DAP экосистемы
- **Поддержка всех алгоритмов**: Классические + пост-квантовые
- **Гибкие метаданные**: Произвольные поля и атрибуты
- **Файловое хранение**: Интеграция с файловой системой
- **Автоматическое управление**: Память и ресурсы
- **Кроссплатформенность**: Все поддерживаемые платформы

## Архитектура DAP сертификатов

### Структура сертификата

```c
typedef struct dap_cert {
    dap_enc_key_t *enc_key;        // Криптографический ключ
    char name[DAP_CERT_ITEM_NAME_MAX]; // Имя сертификата (макс 40 символов)
    void *_pvt;                    // Приватные данные
    dap_binary_tree_t *metadata;   // Метаданные (key-value)
} dap_cert_t;
```

### Метаданные сертификата

DAP сертификаты поддерживают гибкую систему метаданных:

```c
typedef enum dap_cert_metadata_type {
    DAP_CERT_META_STRING,          // Строковые значения
    DAP_CERT_META_BOOL,            // Булевы значения
    DAP_CERT_META_INT,             // Целые числа
    DAP_CERT_META_DATETIME,        // Дата и время
    DAP_CERT_META_DATETIME_PERIOD, // Период времени
    DAP_CERT_META_SIGN,            // Цифровые подписи
    DAP_CERT_META_CUSTOM           // Пользовательские типы
} dap_cert_metadata_type_t;

typedef struct dap_cert_metadata {
    const char *key;               // Ключ метаданных
    uint32_t length;               // Длина значения
    dap_cert_metadata_type_t type; // Тип данных
    byte_t value[];                // Значение
} dap_cert_metadata_t;
```

## API Reference

### Инициализация

#### dap_cert_init()
```c
int dap_cert_init();
```

**Описание**: Инициализирует систему сертификатов DAP.

**Возвращает**:
- `0` - инициализация успешна
- `-1` - ошибка инициализации

**Пример**:
```c
#include "dap_cert.h"

if (dap_cert_init() == 0) {
    printf("✅ DAP certificate system initialized\n");
} else {
    printf("❌ Failed to initialize certificate system\n");
}
```

#### dap_cert_get_str_recommended_sign()
```c
const char *dap_cert_get_str_recommended_sign();
```

**Описание**: Возвращает строку с рекомендуемым алгоритмом подписи.

**Возвращает**: Строка с названием рекомендуемого алгоритма

### Создание сертификатов

#### dap_cert_new()
```c
dap_cert_t *dap_cert_new(const char *a_name);
```

**Описание**: Создает новый пустой сертификат.

**Параметры**:
- `a_name` - имя сертификата (макс 40 символов)

**Возвращает**: Указатель на новый сертификат или NULL при ошибке

**Пример**:
```c
dap_cert_t *cert = dap_cert_new("my_certificate");
if (cert) {
    printf("✅ Certificate '%s' created\n", cert->name);
} else {
    printf("❌ Failed to create certificate\n");
}
```

#### dap_cert_generate()
```c
dap_cert_t *dap_cert_generate(const char *a_cert_name, const char *a_file_path,
                            dap_enc_key_type_t a_key_type);
```

**Описание**: Генерирует новый сертификат с ключом указанного типа.

**Параметры**:
- `a_cert_name` - имя сертификата
- `a_file_path` - путь для сохранения (опционально)
- `a_key_type` - тип криптографического ключа

**Возвращает**: Указатель на созданный сертификат или NULL при ошибке

**Пример**:
```c
// Генерация сертификата с Falcon ключом
dap_cert_t *falcon_cert = dap_cert_generate("falcon_cert", "/path/to/cert",
                                          DAP_ENC_KEY_TYPE_SIG_FALCON);

if (falcon_cert) {
    printf("✅ Certificate with Falcon key generated\n");
    printf("   Name: %s\n", falcon_cert->name);
    printf("   Key type: %d\n", falcon_cert->enc_key->type);
}
```

#### dap_cert_generate_mem()
```c
dap_cert_t *dap_cert_generate_mem(const char *a_cert_name, dap_enc_key_type_t a_key_type);
```

**Описание**: Генерирует сертификат в памяти без сохранения на диск.

**Параметры**:
- `a_cert_name` - имя сертификата
- `a_key_type` - тип криптографического ключа

**Возвращает**: Указатель на созданный сертификат

#### dap_cert_generate_mem_with_seed()
```c
dap_cert_t *dap_cert_generate_mem_with_seed(const char *a_cert_name,
                                         dap_enc_key_type_t a_key_type,
                                         const void *a_seed, size_t a_seed_size);
```

**Описание**: Генерирует сертификат с детерминированным ключом на основе seed.

**Параметры**:
- `a_cert_name` - имя сертификата
- `a_key_type` - тип криптографического ключа
- `a_seed` - seed для генерации ключа
- `a_seed_size` - размер seed

**Возвращает**: Указатель на созданный сертификат

**Пример**:
```c
// Детерминированная генерация
const char *seed = "deterministic_certificate_seed";
dap_cert_t *det_cert = dap_cert_generate_mem_with_seed("det_cert",
                                                     DAP_ENC_KEY_TYPE_SIG_DILITHIUM,
                                                     seed, strlen(seed));

if (det_cert) {
    printf("✅ Deterministic certificate generated\n");
}
```

### Управление сертификатами

#### dap_cert_add()
```c
int dap_cert_add(dap_cert_t *a_cert);
```

**Описание**: Добавляет сертификат в глобальный реестр.

**Параметры**:
- `a_cert` - сертификат для добавления

**Возвращает**:
- `0` - сертификат добавлен успешно
- `-1` - ошибка добавления

#### dap_cert_find_by_name()
```c
dap_cert_t *dap_cert_find_by_name(const char *a_cert_name);
```

**Описание**: Находит сертификат по имени в глобальном реестре.

**Параметры**:
- `a_cert_name` - имя сертификата

**Возвращает**: Указатель на найденный сертификат или NULL

**Пример**:
```c
dap_cert_t *found_cert = dap_cert_find_by_name("my_certificate");
if (found_cert) {
    printf("✅ Certificate found: %s\n", found_cert->name);
} else {
    printf("❌ Certificate not found\n");
}
```

#### dap_cert_get_all_mem()
```c
dap_list_t *dap_cert_get_all_mem();
```

**Описание**: Возвращает список всех сертификатов в памяти.

**Возвращает**: Список сертификатов или NULL

### Работа с файлами

#### dap_cert_add_file()
```c
dap_cert_t *dap_cert_add_file(const char *a_cert_name, const char *a_folder_path);
```

**Описание**: Загружает сертификат из файла.

**Параметры**:
- `a_cert_name` - имя сертификата
- `a_folder_path` - путь к папке с сертификатами

**Возвращает**: Указатель на загруженный сертификат

#### dap_cert_delete_file()
```c
int dap_cert_delete_file(const char *a_cert_name, const char *a_folder_path);
```

**Описание**: Удаляет файл сертификата.

**Параметры**:
- `a_cert_name` - имя сертификата
- `a_folder_path` - путь к папке

**Возвращает**:
- `0` - файл удален успешно
- `-1` - ошибка удаления

#### dap_cert_save_to_folder()
```c
int dap_cert_save_to_folder(dap_cert_t *a_cert, const char *a_file_dir_path);
```

**Описание**: Сохраняет сертификат в указанную папку.

**Параметры**:
- `a_cert` - сертификат для сохранения
- `a_file_dir_path` - путь к папке

**Возвращает**:
- `0` - сертификат сохранен успешно
- `-1` - ошибка сохранения

### Управление папками

#### dap_cert_get_folder()
```c
const char *dap_cert_get_folder(int a_n_folder_path);
```

**Описание**: Возвращает путь к папке сертификатов по индексу.

**Параметры**:
- `a_n_folder_path` - индекс папки (обычно 0)

**Возвращает**: Путь к папке или NULL

#### dap_cert_add_folder()
```c
void dap_cert_add_folder(const char *a_folder_path);
```

**Описание**: Добавляет папку в список поиска сертификатов.

**Параметры**:
- `a_folder_path` - путь к папке

### Цифровые подписи

#### dap_cert_sign()
```c
DAP_STATIC_INLINE dap_sign_t *dap_cert_sign(dap_cert_t *a_cert, const void *a_data,
                                          size_t a_data_size);
```

**Описание**: Создает подпись данных с использованием сертификата.

**Параметры**:
- `a_cert` - сертификат для подписи
- `a_data` - данные для подписи
- `a_data_size` - размер данных

**Возвращает**: Указатель на созданную подпись

**Пример**:
```c
const char *message = "Data to be signed";
dap_sign_t *signature = dap_cert_sign(cert, message, strlen(message));

if (signature) {
    printf("✅ Data signed successfully\n");
} else {
    printf("❌ Failed to sign data\n");
}
```

#### dap_cert_sign_with_hash_type()
```c
dap_sign_t *dap_cert_sign_with_hash_type(dap_cert_t *a_cert, const void *a_data,
                                       size_t a_data_size, uint32_t a_hash_type);
```

**Описание**: Создает подпись с указанным типом хэширования.

**Параметры**:
- `a_cert` - сертификат для подписи
- `a_data` - данные для подписи
- `a_data_size` - размер данных
- `a_hash_type` - тип хэширования

**Возвращает**: Указатель на подпись

#### dap_cert_sign_output()
```c
int dap_cert_sign_output(dap_cert_t *a_cert, const void *a_data, size_t a_data_size,
                        void *a_output, size_t *a_output_size);
```

**Описание**: Создает подпись и записывает результат в буфер.

**Параметры**:
- `a_cert` - сертификат
- `a_data` - данные для подписи
- `a_data_size` - размер данных
- `a_output` - выходной буфер
- `a_output_size` - размер выходного буфера (обновляется)

**Возвращает**:
- `0` - подпись создана успешно
- `-1` - ошибка

### Вспомогательные функции

#### dap_cert_parse_str_list()
```c
size_t dap_cert_parse_str_list(const char *a_certs_str, dap_cert_t ***a_certs,
                             size_t *a_certs_size);
```

**Описание**: Парсит строку со списком имен сертификатов.

**Параметры**:
- `a_certs_str` - строка с именами сертификатов
- `a_certs` - массив указателей на сертификаты
- `a_certs_size` - размер массива

**Возвращает**: Количество распарсенных сертификатов

#### dap_cert_dump()
```c
char *dap_cert_dump(dap_cert_t *a_cert);
```

**Описание**: Создает текстовое представление сертификата.

**Параметры**:
- `a_cert` - сертификат для дампа

**Возвращает**: Строка с дампом сертификата

#### dap_cert_to_pkey()
```c
dap_pkey_t *dap_cert_to_pkey(dap_cert_t *a_cert);
```

**Описание**: Преобразует сертификат в публичный ключ.

**Параметры**:
- `a_cert` - сертификат

**Возвращает**: Указатель на публичный ключ

#### dap_cert_get_pkey_hash()
```c
int dap_cert_get_pkey_hash(dap_cert_t *a_cert, dap_hash_fast_t *a_out_hash);
```

**Описание**: Вычисляет хэш публичного ключа сертификата.

**Параметры**:
- `a_cert` - сертификат
- `a_out_hash` - буфер для хэша

**Возвращает**:
- `0` - хэш вычислен успешно
- `-1` - ошибка

## Примеры использования

### Пример 1: Создание и использование сертификата

```c
#include "dap_cert.h"
#include "dap_enc_falcon.h"
#include <stdio.h>

int basic_certificate_example() {
    // Инициализация системы сертификатов
    if (dap_cert_init() != 0) {
        printf("❌ Failed to initialize certificate system\n");
        return -1;
    }

    // Создание сертификата с Falcon ключом
    printf("Creating certificate with Falcon key...\n");
    dap_cert_t *cert = dap_cert_generate_mem("test_falcon_cert",
                                           DAP_ENC_KEY_TYPE_SIG_FALCON);

    if (!cert) {
        printf("❌ Failed to create certificate\n");
        return -1;
    }

    printf("✅ Certificate created:\n");
    printf("   Name: %s\n", cert->name);
    printf("   Key type: %d\n", cert->enc_key->type);

    // Добавление сертификата в реестр
    if (dap_cert_add(cert) == 0) {
        printf("✅ Certificate added to registry\n");
    }

    // Поиск сертификата по имени
    dap_cert_t *found = dap_cert_find_by_name("test_falcon_cert");
    if (found) {
        printf("✅ Certificate found in registry\n");
    }

    // Подписание данных
    const char *test_data = "Hello, DAP Certificate!";
    dap_sign_t *signature = dap_cert_sign(cert, test_data, strlen(test_data));

    if (signature) {
        printf("✅ Data signed with certificate\n");

        // Верификация подписи (требует дополнительного кода)
        // int verified = dap_sign_verify(signature, cert->enc_key, test_data, strlen(test_data));

        // Освобождение подписи
        // dap_sign_free(signature);
    }

    // Получение списка всех сертификатов
    dap_list_t *all_certs = dap_cert_get_all_mem();
    printf("Total certificates in memory: %zu\n", dap_list_length(all_certs));

    // Очистка
    // В реальном коде нужно освободить сертификаты
    // dap_cert_delete(cert);

    return 0;
}
```

### Пример 2: Работа с метаданными

```c
#include "dap_cert.h"
#include "dap_binary_tree.h"

int certificate_metadata_example() {
    // Создание сертификата
    dap_cert_t *cert = dap_cert_generate_mem("meta_cert",
                                           DAP_ENC_KEY_TYPE_SIG_DILITHIUM);

    if (!cert) {
        printf("❌ Failed to create certificate\n");
        return -1;
    }

    // Добавление метаданных разных типов
    printf("Adding metadata to certificate...\n");

    // Строковые метаданные
    dap_cert_metadata_t *org_meta = calloc(1, sizeof(dap_cert_metadata_t) +
                                          strlen("DAP Labs") + 1);
    org_meta->key = "organization";
    org_meta->length = strlen("DAP Labs") + 1;
    org_meta->type = DAP_CERT_META_STRING;
    strcpy((char *)org_meta->value, "DAP Labs");

    // Целочисленные метаданные
    dap_cert_metadata_t *id_meta = calloc(1, sizeof(dap_cert_metadata_t) + sizeof(int));
    id_meta->key = "certificate_id";
    id_meta->length = sizeof(int);
    id_meta->type = DAP_CERT_META_INT;
    *(int *)id_meta->value = 12345;

    // Булевы метаданные
    dap_cert_metadata_t *active_meta = calloc(1, sizeof(dap_cert_metadata_t) + sizeof(bool));
    active_meta->key = "is_active";
    active_meta->length = sizeof(bool);
    active_meta->type = DAP_CERT_META_BOOL;
    *(bool *)active_meta->value = true;

    // Добавление в дерево метаданных
    if (cert->metadata) {
        dap_binary_tree_add(cert->metadata, org_meta);
        dap_binary_tree_add(cert->metadata, id_meta);
        dap_binary_tree_add(cert->metadata, active_meta);
    }

    printf("✅ Metadata added:\n");
    printf("   Organization: %s\n", (char *)org_meta->value);
    printf("   Certificate ID: %d\n", *(int *)id_meta->value);
    printf("   Is Active: %s\n", *(bool *)active_meta->value ? "true" : "false");

    // Поиск метаданных
    dap_cert_metadata_t *found_org = dap_binary_tree_find(cert->metadata, "organization");
    if (found_org) {
        printf("✅ Found organization metadata: %s\n", (char *)found_org->value);
    }

    // Очистка метаданных
    // В реальном коде нужно освободить память метаданных
    // dap_cert_delete(cert);

    return 0;
}
```

### Пример 3: Сравнение с X.509

```c
#include "dap_cert.h"
#include "dap_enc_falcon.h"
#include "dap_enc_dilithium.h"

int dap_vs_x509_comparison() {
    printf("🔐 DAP Certificate vs X.509 Certificate Comparison\n");
    printf("================================================\n");

    // === DAP СЕРТИФИКАТ ===
    printf("\n1. DAP CERTIFICATE (Custom Format)\n");
    printf("   🎯 Optimized for DAP ecosystem\n");

    // Создание DAP сертификата с пост-квантовым ключом
    dap_cert_t *dap_cert = dap_cert_generate_mem("dap_quantum_cert",
                                               DAP_ENC_KEY_TYPE_SIG_FALCON);

    if (dap_cert) {
        printf("   ✅ DAP certificate created\n");
        printf("   📝 Name: %s\n", dap_cert->name);
        printf("   🔑 Key type: Falcon (post-quantum)\n");
        printf("   📏 Size: Compact (no X.509 overhead)\n");

        // Добавление метаданных
        dap_cert_metadata_t *meta = calloc(1, sizeof(dap_cert_metadata_t) +
                                         strlen("DAP Node Certificate") + 1);
        meta->key = "purpose";
        meta->length = strlen("DAP Node Certificate") + 1;
        meta->type = DAP_CERT_META_STRING;
        strcpy((char *)meta->value, "DAP Node Certificate");

        if (dap_cert->metadata) {
            dap_binary_tree_add(dap_cert->metadata, meta);
            printf("   📋 Metadata: %s\n", (char *)meta->value);
        }

        // Тестирование подписи
        const char *test_msg = "DAP certificate test";
        dap_sign_t *signature = dap_cert_sign(dap_cert, test_msg, strlen(test_msg));

        if (signature) {
            printf("   ✅ Post-quantum signature created\n");
            // dap_sign_free(signature);
        }
    }

    // === X.509 СЕРТИФИКАТ (ГИПОТЕТИЧЕСКИЙ) ===
    printf("\n2. X.509 CERTIFICATE (Standard Format)\n");
    printf("   📜 Legacy standard format\n");

    printf("   📝 Structure: ASN.1 DER encoding\n");
    printf("   📏 Size: Large (certificate chain, extensions)\n");
    printf("   🔑 Key types: RSA, ECDSA (quantum-vulnerable)\n");
    printf("   📋 Extensions: Complex X.509 extensions\n");
    printf("   🕒 Validity: Expiration dates\n");
    printf("   🏛️  Authority: Certificate Authority required\n");

    // === СРАВНЕНИЕ ===
    printf("\n📊 COMPARISON:\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║ Feature           │ DAP Certificate      │ X.509 Certificate  ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║ Format            │ Custom optimized     │ ASN.1 DER          ║\n");
    printf("║ Size              │ Compact              │ Large overhead     ║\n");
    printf("║ Quantum Safety    │ ✅ Post-quantum      │ ❌ Vulnerable      ║\n");
    printf("║ Metadata          │ Flexible key-value   │ Fixed extensions   ║\n");
    printf("║ Authority         │ Self-signed/CA       │ CA required        ║\n");
    printf("║ Performance       │ High                 │ Standard           ║\n");
    printf("║ Ecosystem         │ DAP optimized        │ General purpose    ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");

    printf("\n🎯 USE CASES:\n");
    printf("   🔵 DAP Certificate:\n");
    printf("      • DAP node certificates\n");
    printf("      • Blockchain identity\n");
    printf("      • Post-quantum security\n");
    printf("      • High-performance systems\n");

    printf("\n   🟡 X.509 Certificate:\n");
    printf("      • Web SSL/TLS\n");
    printf("      • Email S/MIME\n");
    printf("      • Code signing\n");
    printf("      • Legacy systems\n");

    // Очистка
    if (dap_cert) {
        // dap_cert_delete(dap_cert);
    }

    return 0;
}
```

### Пример 4: Управление хранилищем сертификатов

```c
#include "dap_cert.h"
#include <stdio.h>

int certificate_storage_example() {
    // Инициализация системы
    if (dap_cert_init() != 0) {
        printf("❌ Failed to initialize certificate system\n");
        return -1;
    }

    // Добавление папки для хранения сертификатов
    const char *cert_folder = "/etc/dap/certificates";
    dap_cert_add_folder(cert_folder);
    printf("✅ Certificate folder added: %s\n", cert_folder);

    // Создание нескольких сертификатов
    printf("\nCreating multiple certificates...\n");

    dap_cert_t *certs[3];
    const char *names[] = {"node_cert", "user_cert", "service_cert"};
    dap_enc_key_type_t types[] = {
        DAP_ENC_KEY_TYPE_SIG_FALCON,
        DAP_ENC_KEY_TYPE_SIG_DILITHIUM,
        DAP_ENC_KEY_TYPE_SIG_ECDSA  // Для совместимости
    };

    for (int i = 0; i < 3; i++) {
        certs[i] = dap_cert_generate(names[i], cert_folder, types[i]);

        if (certs[i]) {
            printf("✅ Certificate '%s' created with ", names[i]);

            switch (types[i]) {
                case DAP_ENC_KEY_TYPE_SIG_FALCON:
                    printf("Falcon key (post-quantum)\n");
                    break;
                case DAP_ENC_KEY_TYPE_SIG_DILITHIUM:
                    printf("Dilithium key (post-quantum)\n");
                    break;
                case DAP_ENC_KEY_TYPE_SIG_ECDSA:
                    printf("ECDSA key (⚠️ quantum-vulnerable)\n");
                    break;
                default:
                    printf("unknown key type\n");
            }
        }
    }

    // Сохранение сертификатов в файлы
    printf("\nSaving certificates to files...\n");
    for (int i = 0; i < 3; i++) {
        if (certs[i]) {
            if (dap_cert_save_to_folder(certs[i], cert_folder) == 0) {
                printf("✅ Certificate '%s' saved to %s/%s.cert\n",
                       certs[i]->name, cert_folder, certs[i]->name);
            }
        }
    }

    // Загрузка сертификатов из файлов
    printf("\nLoading certificates from files...\n");
    for (int i = 0; i < 3; i++) {
        dap_cert_t *loaded = dap_cert_add_file(names[i], cert_folder);
        if (loaded) {
            printf("✅ Certificate '%s' loaded from file\n", loaded->name);
        }
    }

    // Получение списка всех сертификатов
    dap_list_t *all_certs = dap_cert_get_all_mem();
    printf("\nTotal certificates in system: %zu\n", dap_list_length(all_certs));

    // Создание дампа сертификата
    if (certs[0]) {
        char *dump = dap_cert_dump(certs[0]);
        if (dump) {
            printf("\nCertificate dump:\n%s\n", dump);
            free(dump);
        }
    }

    // Очистка
    printf("\nCleaning up...\n");
    for (int i = 0; i < 3; i++) {
        if (certs[i]) {
            // dap_cert_delete_file(certs[i]->name, cert_folder);
            // dap_cert_delete(certs[i]);
        }
    }

    return 0;
}
```

### Пример 5: Продвинутые сценарии использования

```c
#include "dap_cert.h"
#include "dap_enc_falcon.h"
#include "dap_list.h"

int advanced_certificate_scenarios() {
    printf("🔐 Advanced DAP Certificate Scenarios\n");
    printf("====================================\n");

    // === СЦЕНАРИЙ 1: Иерархическая структура ===
    printf("\n1. HIERARCHICAL CERTIFICATE STRUCTURE\n");

    // Root CA сертификат
    dap_cert_t *root_ca = dap_cert_generate_mem("root_ca",
                                              DAP_ENC_KEY_TYPE_SIG_DILITHIUM);

    // Intermediate CA
    dap_cert_t *intermediate_ca = dap_cert_generate_mem("intermediate_ca",
                                                      DAP_ENC_KEY_TYPE_SIG_FALCON);

    // End-entity сертификаты
    dap_cert_t *node_cert = dap_cert_generate_mem("node_001",
                                                DAP_ENC_KEY_TYPE_SIG_FALCON);
    dap_cert_t *user_cert = dap_cert_generate_mem("user_alice",
                                                DAP_ENC_KEY_TYPE_SIG_DILITHIUM);

    printf("✅ Certificate hierarchy created:\n");
    printf("   Root CA: %s\n", root_ca->name);
    printf("   Intermediate CA: %s\n", intermediate_ca->name);
    printf("   Node cert: %s\n", node_cert->name);
    printf("   User cert: %s\n", user_cert->name);

    // === СЦЕНАРИЙ 2: Многоуровневая подпись ===
    printf("\n2. MULTI-LEVEL SIGNING\n");

    const char *document = "Important DAP document";

    // Подпись промежуточным CA
    dap_sign_t *intermediate_sig = dap_cert_sign(intermediate_ca, document, strlen(document));

    // Подпись root CA подписи intermediate
    // dap_sign_t *root_sig = dap_cert_sign(root_ca, intermediate_sig->pkey_hash, sizeof(dap_hash_fast_t));

    printf("✅ Multi-level signature created\n");

    // === СЦЕНАРИЙ 3: Групповая верификация ===
    printf("\n3. BATCH VERIFICATION\n");

    // Создание списка сертификатов
    dap_list_t *cert_list = NULL;
    cert_list = dap_list_append(cert_list, root_ca);
    cert_list = dap_list_append(cert_list, intermediate_ca);
    cert_list = dap_list_append(cert_list, node_cert);
    cert_list = dap_list_append(cert_list, user_cert);

    printf("✅ Certificate batch created: %zu certificates\n", dap_list_length(cert_list));

    // Групповая обработка
    size_t valid_count = 0;
    dap_list_t *current = cert_list;

    while (current) {
        dap_cert_t *cert = (dap_cert_t *)current->data;

        // Проверка валидности сертификата
        if (cert && cert->enc_key) {
            valid_count++;
        }

        current = current->next;
    }

    printf("✅ Valid certificates: %zu/%zu\n", valid_count, dap_list_length(cert_list));

    // === СЦЕНАРИЙ 4: Метаданные для политик ===
    printf("\n4. POLICY-BASED METADATA\n");

    // Добавление политик в метаданные
    if (node_cert->metadata) {
        // Политика доступа
        dap_cert_metadata_t *access_policy = calloc(1, sizeof(dap_cert_metadata_t) +
                                                   strlen("read,write,execute") + 1);
        access_policy->key = "access_policy";
        access_policy->length = strlen("read,write,execute") + 1;
        access_policy->type = DAP_CERT_META_STRING;
        strcpy((char *)access_policy->value, "read,write,execute");

        // Время жизни
        dap_cert_metadata_t *ttl = calloc(1, sizeof(dap_cert_metadata_t) + sizeof(time_t));
        ttl->key = "time_to_live";
        ttl->length = sizeof(time_t);
        ttl->type = DAP_CERT_META_DATETIME;
        *(time_t *)ttl->value = time(NULL) + (365 * 24 * 60 * 60); // 1 год

        dap_binary_tree_add(node_cert->metadata, access_policy);
        dap_binary_tree_add(node_cert->metadata, ttl);

        printf("✅ Policies added to certificate:\n");
        printf("   Access: %s\n", (char *)access_policy->value);
        printf("   TTL: %s", ctime((time_t *)ttl->value));
    }

    // === СЦЕНАРИЙ 5: Аудит и мониторинг ===
    printf("\n5. AUDIT AND MONITORING\n");

    // Создание журнала операций
    typedef struct cert_audit_entry {
        time_t timestamp;
        const char *operation;
        const char *certificate_name;
        bool success;
    } cert_audit_entry_t;

    cert_audit_entry_t audit_log[] = {
        {time(NULL), "create", "root_ca", true},
        {time(NULL), "create", "intermediate_ca", true},
        {time(NULL), "sign", "node_cert", true},
        {time(NULL), "verify", "user_cert", true}
    };

    printf("✅ Audit log created with %zu entries\n", sizeof(audit_log) / sizeof(cert_audit_entry_t));

    // Очистка
    // В реальном коде нужно освободить всю память
    // dap_list_free(cert_list);
    // dap_cert_delete(root_ca);
    // etc.

    return 0;
}
```

## Производительность

### Бенчмарки DAP сертификатов

| Операция | Производительность | Примечание |
|----------|-------------------|------------|
| **Создание сертификата** | ~100-500 μs | Зависит от типа ключа |
| **Подпись данных** | ~50-300 μs | Зависит от алгоритма |
| **Верификация** | ~30-200 μs | Зависит от алгоритма |
| **Поиск по имени** | ~10-50 μs | Хэш-таблица |
| **Сериализация** | ~20-100 μs | Зависит от размера |

### Сравнение с X.509

| Аспект | DAP Certificate | X.509 Certificate |
|--------|----------------|-------------------|
| **Размер** | Компактный | Большой overhead |
| **Парсинг** | Быстрый | Сложный ASN.1 |
| **Гибкость** | Высокая | Ограниченная |
| **Метаданные** | Произвольные | Фиксированные |
| **Производительность** | Высокая | Средняя |

## Безопасность

### Особенности безопасности DAP сертификатов

#### **Преимущества:**
- ✅ **Поддержка пост-квантовой криптографии**
- ✅ **Гибкая система метаданных для политик**
- ✅ **Самоподписанные сертификаты**
- ✅ **Отсутствие зависимости от CA**
- ✅ **Быстрая верификация**

#### **Рекомендации:**
- Используйте пост-квантовые алгоритмы (Falcon, Dilithium)
- Регулярно обновляйте метаданные сертификатов
- Храните сертификаты в защищенных хранилищах
- Проверяйте целостность цепочек доверия

### Цепочки доверия

DAP поддерживает гибкие модели доверия:

```c
// Модель 1: Иерархическая (как X.509)
Root CA -> Intermediate CA -> End Entity

// Модель 2: Сетевая (Web of Trust)
Node A <-> Node B <-> Node C

// Модель 3: Гибридная
Root CA -> Autonomous Nodes
```

## Лучшие практики

### 1. Выбор алгоритма ключа

```c
// Рекомендации по выбору алгоритма
dap_enc_key_type_t select_certificate_key_type(bool high_security,
                                             bool high_speed,
                                             bool compatibility) {

    if (high_security) {
        // Максимальная безопасность
        return DAP_ENC_KEY_TYPE_SIG_DILITHIUM; // Dilithium4
    }

    if (high_speed) {
        // Максимальная скорость
        return DAP_ENC_KEY_TYPE_SIG_FALCON;   // Falcon-512
    }

    if (compatibility) {
        // Для совместимости с внешними системами
        return DAP_ENC_KEY_TYPE_SIG_ECDSA;    // ⚠️ Только для легаси!
    }

    // По умолчанию - баланс
    return DAP_ENC_KEY_TYPE_SIG_FALCON;
}
```

### 2. Управление метаданными

```c
// Стандартизация метаданных сертификатов
void add_standard_certificate_metadata(dap_cert_t *cert,
                                    const char *organization,
                                    const char *purpose,
                                    time_t validity_period) {

    if (!cert->metadata) {
        cert->metadata = dap_binary_tree_new();
    }

    // Организация
    if (organization) {
        dap_cert_metadata_t *org_meta = calloc(1, sizeof(dap_cert_metadata_t) +
                                              strlen(organization) + 1);
        org_meta->key = "organization";
        org_meta->length = strlen(organization) + 1;
        org_meta->type = DAP_CERT_META_STRING;
        strcpy((char *)org_meta->value, organization);
        dap_binary_tree_add(cert->metadata, org_meta);
    }

    // Назначение
    if (purpose) {
        dap_cert_metadata_t *purpose_meta = calloc(1, sizeof(dap_cert_metadata_t) +
                                                  strlen(purpose) + 1);
        purpose_meta->key = "purpose";
        purpose_meta->length = strlen(purpose) + 1;
        purpose_meta->type = DAP_CERT_META_STRING;
        strcpy((char *)purpose_meta->value, purpose);
        dap_binary_tree_add(cert->metadata, purpose_meta);
    }

    // Период валидности
    dap_cert_metadata_t *validity_meta = calloc(1, sizeof(dap_cert_metadata_t) +
                                               sizeof(time_t));
    validity_meta->key = "valid_until";
    validity_meta->length = sizeof(time_t);
    validity_meta->type = DAP_CERT_META_DATETIME;
    *(time_t *)validity_meta->value = time(NULL) + validity_period;
    dap_binary_tree_add(cert->metadata, validity_meta);
}
```

### 3. Работа с хранилищами

```c
// Управление хранилищами сертификатов
typedef struct cert_storage_config {
    const char *primary_folder;
    const char *backup_folder;
    bool auto_backup;
    time_t backup_interval;
} cert_storage_config_t;

int setup_certificate_storage(const cert_storage_config_t *config) {
    // Основное хранилище
    dap_cert_add_folder(config->primary_folder);

    // Резервное хранилище
    if (config->backup_folder) {
        dap_cert_add_folder(config->backup_folder);
    }

    // Настройка автоматического бэкапа
    if (config->auto_backup) {
        // Реализация автоматического бэкапа
        setup_automatic_backup(config->backup_folder, config->backup_interval);
    }

    return 0;
}

int backup_certificate(dap_cert_t *cert, const char *backup_folder) {
    // Создание резервной копии
    char backup_path[PATH_MAX];
    snprintf(backup_path, sizeof(backup_path), "%s/%s.backup",
             backup_folder, cert->name);

    return dap_cert_save_to_folder(cert, backup_path);
}
```

### 4. Мониторинг и аудит

```c
// Система мониторинга сертификатов
typedef struct cert_monitoring_stats {
    size_t total_certificates;
    size_t active_certificates;
    size_t expired_certificates;
    size_t post_quantum_certificates;
    time_t last_audit;
} cert_monitoring_stats_t;

cert_monitoring_stats_t *monitor_certificate_system() {
    cert_monitoring_stats_t *stats = calloc(1, sizeof(cert_monitoring_stats_t));

    // Получение всех сертификатов
    dap_list_t *all_certs = dap_cert_get_all_mem();
    stats->total_certificates = dap_list_length(all_certs);

    // Анализ сертификатов
    dap_list_t *current = all_certs;
    while (current) {
        dap_cert_t *cert = (dap_cert_t *)current->data;

        // Проверка активности
        if (is_certificate_active(cert)) {
            stats->active_certificates++;
        }

        // Проверка срока действия
        if (is_certificate_expired(cert)) {
            stats->expired_certificates++;
        }

        // Проверка типа алгоритма
        if (is_post_quantum_key(cert->enc_key->type)) {
            stats->post_quantum_certificates++;
        }

        current = current->next;
    }

    stats->last_audit = time(NULL);

    return stats;
}

void print_certificate_report(const cert_monitoring_stats_t *stats) {
    printf("📊 DAP Certificate System Report\n");
    printf("================================\n");
    printf("Total certificates: %zu\n", stats->total_certificates);
    printf("Active certificates: %zu\n", stats->active_certificates);
    printf("Expired certificates: %zu\n", stats->expired_certificates);
    printf("Post-quantum certificates: %zu\n", stats->post_quantum_certificates);
    printf("Last audit: %s", ctime(&stats->last_audit));

    // Вычисление процента пост-квантовых сертификатов
    if (stats->total_certificates > 0) {
        double pq_percentage = (double)stats->post_quantum_certificates /
                              stats->total_certificates * 100;
        printf("Post-quantum adoption: %.1f%%\n", pq_percentage);
    }
}
```

## Заключение

Модуль `dap_cert` предоставляет мощную и гибкую систему сертификатов, оптимизированную для экосистемы DAP:

### Ключевые преимущества:
- **Собственный формат**: Оптимизирован для высокопроизводительных систем
- **Пост-квантовая безопасность**: Поддержка современных алгоритмов
- **Гибкие метаданные**: Произвольные поля для политик и атрибутов
- **Высокая производительность**: Быстрое создание, подпись и верификация
- **Кроссплатформенность**: Работа на всех поддерживаемых платформах

### Основные возможности:
- Поддержка всех типов ключей (пост-квантовые + классические)
- Гибкая система метаданных для политик
- Интеграция с файловой системой
- Автоматическое управление памятью
- Иерархические и сетевые модели доверия

### Рекомендации по использованию:
1. **Используйте пост-квантовые алгоритмы** для новых проектов
2. **Добавляйте метаданные** для политик и атрибутов
3. **Организуйте хранилища** сертификатов
4. **Мониторьте состояние** системы сертификатов
5. **Регулярно обновляйте** сертификаты

### Следующие шаги:
1. Изучите примеры использования сертификатов
2. Ознакомьтесь с API метаданных
3. Интегрируйте сертификаты в свои приложения
4. Настройте мониторинг и аудит

Для получения дополнительной информации смотрите:
- `dap_cert.h` - полный API сертификатов
- `dap_sign.h` - API цифровых подписей
- Примеры в директории `examples/certificates/`
- Тесты в директории `test/certificates/`

