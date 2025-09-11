# DAP Global DB Module (dap_global_db.h)

## Обзор

Модуль `dap_global_db` является высокопроизводительной распределенной системой хранения ключ-значение для DAP SDK. Он предоставляет:

- **Множественные драйверы хранения** - MDBX, PostgreSQL, SQLite
- **Распределенная синхронизация** - автоматическая репликация между узлами
- **Кластеризация** - поддержка кластеров с ролями и правами
- **Криптографическая защита** - подписывание и верификация данных
- **Временные метки** - наносекундная точность с историей изменений

## Архитектурная роль

Global DB является центральным хранилищем данных в экосистеме DAP:

```
┌─────────────────┐    ┌─────────────────┐
│   DAP SDK       │───▶│  Global DB      │
│   Приложения    │    │                 │
└─────────────────┘    └─────────────────┘
         │                       │
    ┌────▼────┐             ┌────▼────┐
    │Блокчейн  │             │Драйверы │
    │данные    │             │Хранения │
    └─────────┘             └─────────┘
         │                       │
    ┌────▼────┐             ┌────▼────┐
    │Сеть P2P  │◄────────────►│Кластеры │
    │синхр.    │             │узлов    │
    └─────────┘             └─────────┘
```

## Основные компоненты

### 1. Экземпляр базы данных
```c
typedef struct dap_global_db_instance {
    uint32_t version;              // Версия GlobalDB
    char *storage_path;            // Путь к хранилищу
    char *driver_name;             // Имя драйвера
    dap_list_t *whitelist;         // Белый список
    dap_list_t *blacklist;         // Черный список
    uint64_t store_time_limit;     // Лимит времени хранения
    dap_global_db_cluster_t *clusters; // Кластеры
    dap_enc_key_t *signing_key;    // Ключ подписи
    uint32_t sync_idle_time;       // Время простоя синхронизации
} dap_global_db_instance_t;
```

### 2. Объект хранения
```c
typedef struct dap_global_db_obj {
    char *key;                     // Ключ
    uint8_t *value;                // Значение
    size_t value_len;              // Длина значения
    dap_nanotime_t timestamp;      // Временная метка
    bool is_pinned;                // Флаг закрепления
} dap_global_db_obj_t;
```

### 3. Хранимый объект драйвера
```c
typedef struct dap_store_obj {
    char *group;                   // Группа (аналог таблицы)
    char *key;                     // Ключ
    byte_t *value;                 // Значение
    size_t value_len;              // Длина значения
    uint8_t flags;                 // Флаги записи
    dap_sign_t *sign;              // Криптографическая подпись
    dap_nanotime_t timestamp;      // Временная метка
    uint64_t crc;                  // Контрольная сумма
    byte_t ext[];                  // Дополнительные данные
} dap_store_obj_t;
```

## Поддерживаемые драйверы

### MDBX (рекомендуемый)
- **Высокая производительность** - оптимизированная B-дерево структура
- **ACID транзакции** - полная атомарность операций
- **MVCC** - многовариантный контроль конкуренции
- **Crash resistance** - устойчивость к сбоям

### PostgreSQL
- **SQL совместимость** - стандартные SQL запросы
- **Масштабируемость** - поддержка больших объемов данных
- **Репликация** - встроенные механизмы репликации
- **Расширения** - поддержка дополнительных модулей

### SQLite
- **Встраиваемость** - нулевая конфигурация
- **Простота** - минимальные зависимости
- **Надежность** - проверенная временем
- **Кроссплатформенность** - работает везде

## Основные операции

### Инициализация и управление

#### `dap_global_db_init()`
```c
int dap_global_db_init();
```

Инициализирует систему Global DB.

**Возвращаемые значения:**
- `0` - успешная инициализация
- `-1` - ошибка инициализации

#### `dap_global_db_deinit()`
```c
void dap_global_db_deinit();
```

Деинициализирует систему Global DB.

### Синхронные операции

#### Чтение данных
```c
// Получить значение по ключу
byte_t *dap_global_db_get_sync(const char *a_group, const char *a_key,
                              size_t *a_data_size, bool *a_is_pinned,
                              dap_nanotime_t *a_ts);

// Получить последнее значение в группе
byte_t *dap_global_db_get_last_sync(const char *a_group, char **a_key,
                                   size_t *a_data_size, bool *a_is_pinned,
                                   dap_nanotime_t *a_ts);

// Получить все значения в группе
dap_global_db_obj_t *dap_global_db_get_all_sync(const char *a_group,
                                              size_t *a_objs_count);
```

#### Запись данных
```c
// Установить значение
int dap_global_db_set_sync(const char *a_group, const char *a_key,
                          const void *a_value, const size_t a_value_length,
                          bool a_pin_value);

// Закрепить/открепить значение
int dap_global_db_pin_sync(const char *a_group, const char *a_key);
int dap_global_db_unpin_sync(const char *a_group, const char *a_key);

// Удалить значение
int dap_global_db_del_sync(const char *a_group, const char *a_key);
```

#### Управление данными
```c
// Очистить всю группу
int dap_global_db_erase_table_sync(const char *a_group);

// Сбросить изменения на диск
int dap_global_db_flush_sync();
```

### Асинхронные операции

#### Асинхронное чтение
```c
// Получить значение асинхронно
int dap_global_db_get(const char *a_group, const char *a_key,
                     dap_global_db_callback_result_t a_callback,
                     void *a_arg);

// Получить все значения асинхронно
int dap_global_db_get_all(const char *a_group, size_t l_results_page_size,
                         dap_global_db_callback_results_t a_callback,
                         void *a_arg);
```

**Тип callback функции:**
```c
typedef void (*dap_global_db_callback_result_t)(
    dap_global_db_instance_t *a_dbi, int a_rc, const char *a_group,
    const char *a_key, const void *a_value, const size_t a_value_size,
    dap_nanotime_t a_value_ts, bool a_is_pinned, void *a_arg);
```

#### Асинхронная запись
```c
// Установить значение асинхронно
int dap_global_db_set(const char *a_group, const char *a_key,
                     const void *a_value, const size_t a_value_length,
                     bool a_pin_value,
                     dap_global_db_callback_result_t a_callback,
                     void *a_arg);

// Установить несколько значений
int dap_global_db_set_multiple_zc(const char *a_group,
                                 dap_global_db_obj_t *a_values,
                                 size_t a_values_count,
                                 dap_global_db_callback_results_t a_callback,
                                 void *a_arg);
```

## Кластеризация и синхронизация

### Архитектура кластеров
```
┌─────────────┐    ┌─────────────┐
│ Master Node │◄──►│ Slave Node  │
│             │    │             │
│ ┌─────────┐ │    │ ┌─────────┐ │
│ │Global DB│ │    │ │Global DB│ │
│ └─────────┘ │    │ └─────────┘ │
└─────────────┘    └─────────────┘
       │                │
       └────────────────┘
          Синхронизация
```

### Роли узлов
- **Master** - основной узел, принимает записи
- **Slave** - подчиненный узел, получает репликацию
- **Witness** - свидетель, проверяет целостность

### Синхронизация данных
```c
#define DAP_GLOBAL_DB_SYNC_WAIT_TIMEOUT 5 // Таймаут ожидания синхронизации
```

## Криптографическая защита

### Подписание данных
```c
// Ключ подписи в экземпляре
dap_enc_key_t *signing_key; // Используется для подписи всех записей
```

### Верификация целостности
```c
// CRC контрольная сумма
uint64_t crc; // Вычисляется для каждой записи

// Криптографическая подпись
dap_sign_t *sign; // Подпись для аутентификации
```

### Флаги записей
```c
#define DAP_GLOBAL_DB_RECORD_DEL     BIT(0)  // Запись удалена
#define DAP_GLOBAL_DB_RECORD_PINNED  BIT(1)  // Запись закреплена
#define DAP_GLOBAL_DB_RECORD_NEW     BIT(6)  // Новая запись
#define DAP_GLOBAL_DB_RECORD_ERASE   BIT(7)  // Запись для окончательного удаления
```

## Производительность и оптимизации

### Оптимизации хранения
- **Сжатие данных** - уменьшение размера хранилища
- **Индексация** - быстрый поиск по ключам
- **Кэширование** - in-memory кэш для частых запросов
- **Пакетные операции** - группировка для снижения overhead

### Масштабируемость
- **Горизонтальное масштабирование** - распределение по кластерам
- **Вертикальное масштабирование** - увеличение ресурсов узла
- **Автоматическая балансировка** - распределение нагрузки

### Мониторинг
```c
extern int g_dap_global_db_debug_more; // Расширенное логирование
```

## Использование

### Базовая инициализация

```c
#include "dap_global_db.h"

// Инициализация
if (dap_global_db_init() != 0) {
    fprintf(stderr, "Failed to initialize Global DB\n");
    return -1;
}

// Получение экземпляра
dap_global_db_instance_t *dbi = dap_global_db_instance_get_default();

// Основные операции
// ... использование ...

// Деинициализация
dap_global_db_deinit();
```

### Синхронные операции

```c
// Запись данных
const char *group = "user_data";
const char *key = "user123";
const char *value = "user information";
size_t value_len = strlen(value);

int result = dap_global_db_set_sync(group, key, value, value_len, false);
if (result != 0) {
    fprintf(stderr, "Failed to set value\n");
}

// Чтение данных
size_t data_size;
bool is_pinned;
dap_nanotime_t timestamp;

byte_t *data = dap_global_db_get_sync(group, key, &data_size,
                                     &is_pinned, &timestamp);
if (data) {
    printf("Value: %.*s\n", (int)data_size, data);
    free(data);
}
```

### Асинхронные операции

```c
void on_db_result(dap_global_db_instance_t *dbi, int rc,
                 const char *group, const char *key,
                 const void *value, size_t value_size,
                 dap_nanotime_t ts, bool is_pinned, void *arg) {
    if (rc == DAP_GLOBAL_DB_RC_SUCCESS) {
        printf("Async read successful: %.*s\n", (int)value_size, (char*)value);
    }
}

// Асинхронное чтение
dap_global_db_get("user_data", "user123", on_db_result, NULL);
```

### Работа с группами

```c
// Получение всех записей в группе
size_t count;
dap_global_db_obj_t *objs = dap_global_db_get_all_sync("user_data", &count);

for (size_t i = 0; i < count; i++) {
    printf("Key: %s, Value: %.*s\n",
           objs[i].key, (int)objs[i].value_len, objs[i].value);
}

// Освобождение памяти
dap_global_db_objs_delete(objs, count);
```

## Коды возврата

```c
#define DAP_GLOBAL_DB_RC_SUCCESS     0  // Успех
#define DAP_GLOBAL_DB_RC_NOT_FOUND   1  // Запись не найдена
#define DAP_GLOBAL_DB_RC_PROGRESS    2  // Операция в процессе
#define DAP_GLOBAL_DB_RC_NO_RESULTS -1  // Нет результатов
#define DAP_GLOBAL_DB_RC_CRITICAL   -3  // Критическая ошибка
#define DAP_GLOBAL_DB_RC_ERROR      -6  // Общая ошибка
```

## Интеграция с другими модулями

### DAP Chain
- Хранение блокчейн данных
- Кэширование состояний
- Синхронизация между узлами

### DAP Net
- P2P синхронизация данных
- Распределенное хранение
- Репликация между узлами

### DAP Crypto
- Криптографическая защита данных
- Подписание транзакций
- Верификация целостности

## Лучшие практики

### 1. Производительность
```c
// Используйте асинхронные операции для высокой нагрузки
dap_global_db_set(group, key, value, size, pin, callback, arg);

// Группируйте операции в транзакции
dap_global_db_driver_txn_start();
// ... несколько операций ...
dap_global_db_driver_txn_end(true); // commit
```

### 2. Безопасность
```c
// Всегда проверяйте коды возврата
if (dap_global_db_set_sync(group, key, value, size, pin) != 0) {
    // Обработка ошибки
}

// Используйте закрепление для важных данных
dap_global_db_pin_sync("critical_data", "important_key");
```

### 3. Управление ресурсами
```c
// Освобождайте память после использования
if (data) free(data);

// Используйте flush для принудительной записи
dap_global_db_flush_sync();
```

## Типичные проблемы

### 1. Конфликты синхронизации
```
Симптом: Несогласованные данные между узлами
Решение: Проверить настройки кластера и роли узлов
```

### 2. Производительность записи
```
Симптом: Медленная запись при высокой нагрузке
Решение: Использовать MDBX драйвер и асинхронные операции
```

### 3. Утечки памяти
```
Симптом: Рост потребления памяти
Решение: Правильно освобождать объекты dap_global_db_obj_t
```

## Заключение

Модуль `dap_global_db` предоставляет надежную и высокопроизводительную систему хранения данных, оптимизированную для распределенных приложений DAP. Его архитектура обеспечивает целостность, безопасность и масштабируемость данных в сетевой среде.
