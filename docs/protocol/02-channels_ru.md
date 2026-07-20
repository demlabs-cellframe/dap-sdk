# DAP Stream Channels — Мультиплексирование каналов

> **Версия:** 1.0 | **Дата:** 2026-07-20 | **Модуль:** `dap-sdk/net/stream/ch/`

## Обзор

Каналы (Channels) — четвёртый уровень сетевого стека DAP SDK. Предоставляют мультиплексирование нескольких логических потоков данных внутри одного бинарного потока `dap_stream_t`. Каждый канал имеет свой тип (VPN, GlobalDB, Chain, Gossip и др.), определяемый через vtable-процессор с четырьмя callback-функциями.

## Архитектура

```
┌──────────────────────────────────────────────────────────┐
│ L4: Каналы (dap_stream_ch_t) ← ЭТОТ ДОКУМЕНТ            │
│     VPN ('S'), GlobalDB ('D'), Chain Net ('N'), Gossip ('G') │
├──────────────────────────────────────────────────────────┤
│ L3: DAP Stream Protocol                                  │
│     dap_stream_t → dap_stream_pkt_t                      │
├──────────────────────────────────────────────────────────┤
│ L2: Transport Abstraction (dap_net_trans_t)              │
├──────────────────────────────────────────────────────────┤
│ L1: TLS / UDP / HTTP / DNS / WebSocket                   │
├──────────────────────────────────────────────────────────┤
│ L0: IO (dap_events_socket_t)                             │
└──────────────────────────────────────────────────────────┘
```

## Структура канала: dap_stream_ch_t

Каждый экземпляр `dap_stream_ch_t` представляет один логический канал внутри потока:

```c
typedef struct dap_stream_ch {
    pthread_mutex_t         mutex;               // Мьютекс для потокобезопасности
    bool                    ready_to_write;       // Готовность к записи
    bool                    ready_to_read;        // Готовность к чтению
    bool                    closing;              // Флаг закрытия канала
    dap_stream_t            *stream;              // Родительский поток
    dap_stream_ch_uuid_t    uuid;                 // Уникальный ID канала
    dap_stream_worker_t     *stream_worker;       // Воркер, владеющий каналом
    struct {
        uint64_t            bytes_write;          // Байт записано
        uint64_t            bytes_read;           // Байт прочитано
    } stat;
    dap_list_t              *packet_in_notifiers; // Список нотификаторов входящих пакетов
    dap_list_t              *packet_out_notifiers;// Список нотификаторов исходящих пакетов
    dap_dap_stream_ch_proc_t *proc;               // Vtable процессора (тип канала)
    void                    *internal;            // Приватные данные конкретного канала
    struct dap_stream_ch    *me;                  // Self-указатель
    UT_hash_handle          hh_worker;            // Хеш-таблица для поиска по воркеру
} dap_stream_ch_t;
```

### Поле mutex

Поле `mutex` (`pthread_mutex_t`) защищает состояние канала при изменении флага `closing` в процессе удаления. Гарантирует, что callback удаления (`delete_callback`) будет вызван до освобождения ресурсов канала.

### Флаги состояния

| Флаг | Тип | Описание |
|------|-----|----------|
| `ready_to_write` | `bool` | Канал готов к записи данных. Управляется через `dap_stream_ch_set_ready_to_write_unsafe()` |
| `ready_to_read` | `bool` | Канал готов к чтению данных. Устанавливается в `true` при создании |
| `closing` | `bool` | Канал в процессе закрытия. Устанавливается в `dap_stream_ch_delete()`, блокирует дальнейшие операции |

### Поле proc

Указатель на `dap_stream_ch_proc_t` — vtable, определяющий тип канала и его обработчики. Устанавливается при создании через lookup по ID.

### Поле internal

Указатель `void*` для хранения специфичных для типа канала данных. Например, VPN-канал хранит здесь контекст VPN-соединения, канал GlobalDB — состояние репликации. Должен быть освобождён в `delete_callback`.

### Статистика

Вложенный анонимный блок `stat` содержит счётчики:
- `bytes_write` — общий объём записанных данных (без заголовков)
- `bytes_read` — общий объём прочитанных данных

### Хеш-таблица hh_worker

Каждый канал добавляется в хеш-таблицу `stream_worker->channels` с ключом `uuid`. Это обеспечивает быстрый поиск канала по UUID в контексте воркера. Хеш-таблица защищена `channels_rwlock` (см. раздел «Потокобезопасность»).

## UUID канала

```c
typedef unsigned int dap_stream_ch_uuid_t;
```

UUID канала — 32-битный беззнаковый целочисленный идентификатор. Генерируется атомарно при создании канала:

```c
unsigned int dap_new_stream_ch_id() {
    static _Atomic unsigned int stream_ch_id = 0;
    return stream_ch_id++;
}
```

Атомарный инкремент гарантирует уникальность ID даже при одновременном создании каналов из разных потоков.

## Пакет канала: dap_stream_ch_pkt_t

### Заголовок пакета

```c
typedef struct dap_stream_ch_pkt_hdr {
    uint8_t     id;           // ID канала (символ, напр. 'S', 'D', 'N')
    uint8_t     enc_type;     // Тип шифрования (0 = не зашифрован)
    uint8_t     type;         // Тип пакета (определяется каналом)
    uint8_t     padding;      // Выравнивание
    uint64_t    seq_id;       // Sequence ID (порядковый номер в потоке)
    uint32_t    data_size;    // Размер данных payload
} DAP_ALIGN_PACKED dap_stream_ch_pkt_hdr_t;  // 16 bytes
```

### Wire format заголовка

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|   id (1)      |  enc_type (1) |   type (1)    |  padding (1)  |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                       seq_id (8 bytes)                        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                   seq_id cont.                | data_size (4) |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                   data_size cont.             |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

**Итого заголовок:** 16 bytes (`1 + 1 + 1 + 1 + 8 + 4`).

### Полный пакет

```c
typedef struct dap_stream_ch_pkt {
    dap_stream_ch_pkt_hdr_t hdr;
    uint8_t                 data[];    // Flexible array member
} DAP_ALIGN_PACKED dap_stream_ch_pkt_t;
```

### Типы пакетов

| Тип | Значение | Описание |
|-----|----------|----------|
| `STREAM_CH_PKT_TYPE_REQUEST` | `0x0` | Запрос (по умолчанию) |

Конкретные типы пакетов определяются каждым каналом индивидуально. Например, VPN-канал может использовать типы для управления туннелем, а канал GlobalDB — для команд репликации.

## Процессор канала: dap_stream_ch_proc_t

Vtable, определяющий поведение конкретного типа канала:

```c
typedef struct dap_stream_ch_proc {
    uint8_t                         id;                 // Символьный ID типа канала
    dap_stream_ch_callback_t        new_callback;       // Вызывается при создании канала
    dap_stream_ch_callback_t        delete_callback;    // Вызывается при удалении канала
    dap_stream_ch_read_callback_t   packet_in_callback; // Обработчик входящих пакетов (возвращает bool)
    dap_stream_ch_write_callback_t  packet_out_callback;// Обработчик исходящих пакетов (возвращает bool)
    void                            *internal;          // Приватные данные процессора
} dap_stream_ch_proc_t;
```

### Callback-функции

| Callback | Сигнатура | Описание |
|----------|-----------|----------|
| `new_callback` | `void (*)(dap_stream_ch_t*, void*)` | Инициализация канала. Создаёт `internal` данные |
| `delete_callback` | `void (*)(dap_stream_ch_t*, void*)` | Деструкция канала. Освобождает `internal` данные |
| `packet_in_callback` | `bool (*)(dap_stream_ch_t*, void*)` | Обработка входящего пакета. Возвращает `true` при успехе |
| `packet_out_callback` | `bool (*)(dap_stream_ch_t*, void*)` | Обработка исходящего пакета. Возвращает `true` при успехе |

## Регистрация каналов

### Инициализация модуля

```c
int stream_ch_proc_init();     // Инициализация реестра процессоров
void stream_ch_proc_deinit();  // Деинициализация
```

### Глобальный реестр

Процессоры каналов хранятся в статическом массиве из 256 элементов, индексированном по ID (символу):

```c
dap_stream_ch_proc_t s_proc[256] = {{0}};
```

### Регистрация нового типа канала

```c
void dap_stream_ch_proc_add(
    uint8_t                         id,                 // ID типа (символ, напр. 'S')
    dap_stream_ch_callback_t        new_callback,       // Callback создания
    dap_stream_ch_callback_t        delete_callback,    // Callback удаления
    dap_stream_ch_read_callback_t   packet_in_callback, // Обработчик входящих пакетов
    dap_stream_ch_write_callback_t  packet_out_callback // Обработчик исходящих пакетов
);
```

Пример регистрации VPN-канала:
```c
dap_stream_ch_proc_add('S', vpn_ch_new, vpn_ch_delete, vpn_ch_pkt_in, vpn_ch_pkt_out);
```

### Поиск процессора

```c
dap_stream_ch_proc_t *dap_stream_ch_proc_find(uint8_t id);
```

Возвращает указатель на `s_proc[id]`. Если процессор не был зарегистрирован, возвращается указатель на нулевую запись (все callback = NULL).

### Технический канал

```c
#define TECHICAL_CHANNEL_ID 't'
```

Зарезервированный ID для технического (служебного) канала.

## Жизненный цикл канала

### Создание: dap_stream_ch_new()

```
1. Вызов dap_stream_ch_proc_find(a_id) — поиск процессора по ID
2. Если процессор не найден → возврат NULL
3. DAP_REALLOC_COUNT — расширение массива каналов в потоке
4. dap_stream_ch_alloc() — выделение памяти под dap_stream_ch_t
5. Инициализация полей:
   - me = self
   - stream = родительский поток
   - proc = найденный процессор
   - ready_to_read = true
   - closing = false
   - uuid = dap_new_stream_ch_id() (атомарный инкремент)
   - pthread_mutex_init(&mutex)
6. Поиск stream_worker из потока
7. pthread_rwlock_wrlock(&channels_rwlock)
8. HASH_ADD_BYHASHVALUE — добавление в хеш-таблицу воркера
9. pthread_rwlock_unlock(&channels_rwlock)
10. Вызов proc->new_callback(ch, NULL)
11. Добавление в массив channel[] потока, увеличение channel_count
12. Возврат указателя на канал
```

### Удаление: dap_stream_ch_delete()

```
1. pthread_rwlock_wrlock(&channels_rwlock)
2. HASH_DELETE(hh_worker) — удаление из хеш-таблицы воркера
3. pthread_rwlock_unlock(&channels_rwlock)
4. pthread_mutex_lock(&ch->mutex)
5. closing = true
6. Вызов proc->delete_callback(ch, NULL)
7. assert(!ch->internal) — internal должен быть обнулён в delete_callback
8. Поиск индекса канала в массиве stream->channel[]
9. Сдвиг массива для заполнения «дырки»
10. Если каналов не осталось → освобождение массива
11. pthread_mutex_unlock(&ch->mutex)
12. Отложенное освобождение через dap_worker_exec_callback_on(worker, s_stream_ch_free_callback, ch)
    — предотвращает use-after-free при итерации нотификаторов
13. Если воркер недоступен → прямое освобождение dap_stm_ch_free()
```

### Освобождение памяти: dap_stm_ch_free()

```
1. (DAP_SYS_DEBUG) Поиск записи в трекинговой хеш-таблице s_stm_chs
2. HASH_DEL из s_stm_chs
3. dap_list_free_full(packet_in_notifiers)
4. dap_list_free_full(packet_out_notifiers)
5. DAP_DELETE(ch) — освобождение структуры канала
```

## Потокобезопасность

### channels_rwlock

Хеш-таблица каналов воркера (`stream_worker->channels`) защищена `pthread_rwlock_t channels_rwlock`:

- **Чтение** (`pthread_rwlock_rdlock`): поиск канала по UUID, итерация
- **Запись** (`pthread_rwlock_wrlock`): добавление/удаление канала

```c
// Поиск (только чтение)
pthread_rwlock_rdlock(&a_worker->channels_rwlock);
HASH_FIND_BYHASHVALUE(hh_worker, a_worker->channels, &a_uuid, sizeof(a_uuid), a_uuid, l_ch);
pthread_rwlock_unlock(&a_worker->channels_rwlock);
```

### Per-channel mutex

Мьютекс `ch->mutex` защищает состояние `closing` и обеспечивает атомарность операции удаления. Гарантирует, что `delete_callback` будет вызван ровно один раз.

### Отложенное освобождение (Deferred Free)

Канал не освобождается немедленно в `dap_stream_ch_delete()`. Вместо этого освобождение планируется через `dap_worker_exec_callback_on()`:

```c
// Defer actual free to worker's queue to avoid freeing while iterating notifiers
if (l_stream_worker && l_stream_worker->worker)
    dap_worker_exec_callback_on(l_stream_worker->worker, s_stream_ch_free_callback, a_ch);
else
    dap_stm_ch_free(a_ch);
```

Это предотвращает use-after-free, когда `dap_stream_ch_delete()` вызывается из callback-а нотификатора во время итерации по `packet_in_notifiers` или `packet_out_notifiers`.

## Функции записи

### Однопоточная запись: dap_stream_ch_pkt_write_unsafe()

```c
size_t dap_stream_ch_pkt_write_unsafe(
    dap_stream_ch_t *a_ch,      // Канал (UNSAFE: только в контексте worker)
    uint8_t         a_type,     // Тип пакета
    const void      *a_data,    // Данные
    size_t          a_data_size // Размер данных
);
```

Прямая запись в канал из контекста воркера. Формирует заголовок `dap_stream_ch_pkt_hdr_t`, выполняет фрагментацию при необходимости, передаёт в `dap_stream_pkt_write_unsafe()`.

### Многопоточная запись: dap_stream_ch_pkt_write_mt()

```c
size_t dap_stream_ch_pkt_write_mt(
    dap_stream_worker_t  *a_worker,   // Целевой воркер
    dap_stream_ch_uuid_t a_ch_uuid,   // UUID канала
    uint8_t              a_type,      // Тип пакета
    const void           *a_data,     // Данные
    size_t               a_data_size  // Размер данных
);
```

Кросс-поточная запись. Копирует данные и отправляет через очередь `queue_ch_io`. Если вызов идёт из другого воркера, использует межворкерную очередь `queue_ch_io_input[src][dst]`.

### Форматированная запись (printf-style)

```c
// Однопоточная
ssize_t dap_stream_ch_pkt_write_f_unsafe(dap_stream_ch_t *a_ch, uint8_t a_type, const char *a_format, ...);

// Многопоточная
size_t dap_stream_ch_pkt_write_f_mt(dap_stream_worker_t *a_worker, dap_stream_ch_uuid_t a_ch_uuid,
                                     uint8_t a_type, const char *a_format, ...);
```

Выполняют `vsnprintf()` для форматирования строки, затем передают результат в соответствующую функцию записи.

### Отправка по esocket UUID

```c
int dap_stream_ch_pkt_send_mt(
    dap_stream_worker_t     *a_worker,  // Воркер
    dap_events_socket_uuid_t a_uuid,    // UUID events socket
    char                     a_ch_id,   // ID канала (символ)
    uint8_t                  a_type,    // Тип пакета
    const void               *a_data,   // Данные
    size_t                   a_data_size// Размер данных
);
```

Отправка данных в канал по UUID events socket. Данные копируются и передаются через очередь `queue_ch_send`.

### Отправка по адресу узла

```c
int dap_stream_ch_pkt_send_by_addr(
    dap_stream_node_addr_t *a_addr,     // Адрес целевого узла
    char                    a_ch_id,    // ID канала
    uint8_t                 a_type,     // Тип пакета
    const void              *a_data,    // Данные
    size_t                  a_data_size // Размер данных
);
```

Выполняет lookup stream по адресу узла через `dap_stream_find_by_addr()`, затем вызывает `dap_stream_ch_pkt_send_mt()`.

### Межворкерная запись: dap_stream_ch_pkt_write_inter()

```c
size_t dap_stream_ch_pkt_write_inter(
    dap_context_queue_t     *a_queue_input,  // Целевая очередь
    dap_stream_ch_uuid_t    a_ch_uuid,       // UUID канала
    uint8_t                 a_type,          // Тип пакета
    const void              *a_data,         // Данные
    size_t                  a_data_size      // Размер данных
);
```

Прямая отправка в указанную контекстную очередь. Используется для межворкерной маршрутизации.

## Фрагментация в канальных пакетах

Когда размер данных канала превышает MTU транспорта, автоматически выполняется фрагментация.

### Алгоритм

```
1. Определение целевого размера (l_target_size):
   - Если транспорт поддерживает get_max_packet_size() → использовать его значение
   - Иначе → DAP_STREAM_PKT_FRAGMENT_SIZE
2. Вычисление максимального размера фрагмента:
   l_max_fragm_size = l_target_size
                    - DAP_STREAM_PKT_ENCRYPTION_OVERHEAD
                    - sizeof(dap_stream_fragment_pkt_t)
3. Если l_data_size <= l_max_fragm_size:
   - Пакет помещается целиком → STREAM_PKT_TYPE_DATA_PACKET
4. Если l_data_size > l_max_fragm_size:
   - Нарезка на фрагменты с заполнением size, mem_shift, full_size
   - Первый фрагмент включает заголовок канала (dap_stream_ch_pkt_hdr_t)
   - Последующие фрагменты — чистые данные
   - Каждый фрагмент отправляется как STREAM_PKT_TYPE_FRAGMENT_PACKET
```

### Статистика

После отправки обновляется `ch->stat.bytes_write += a_data_size` (без учёта заголовков).

## Система нотификаторов

Нотификаторы позволяют внешним модулям получать уведомления о входящих/исходящих пакетах конкретного канала.

### Структура нотификатора

```c
typedef struct dap_stream_ch_notifier {
    dap_stream_ch_notify_callback_t callback;  // Callback-функция
    void                            *arg;      // Пользовательские данные
} dap_stream_ch_notifier_t;
```

### Callback-сигнатура

```c
typedef void (*dap_stream_ch_notify_callback_t)(
    dap_stream_ch_t *a_ch,        // Канал
    uint8_t          a_type,      // Тип пакета
    const void       *a_data,     // Данные пакета
    size_t           a_data_size, // Размер данных
    void             *a_arg       // Пользовательские данные
);
```

### Регистрация

```c
int dap_stream_ch_add_notifier(
    dap_stream_node_addr_t           *a_stream_addr,  // Адрес узла
    uint8_t                          a_ch_id,         // ID канала (символ)
    dap_stream_packet_direction_t    a_direction,     // DAP_STREAM_PKT_DIR_IN / DAP_STREAM_PKT_DIR_OUT
    dap_stream_ch_notify_callback_t  a_callback,      // Callback
    void                             *a_callback_arg  // Аргумент callback
);
```

### Удаление

```c
int dap_stream_ch_del_notifier(
    dap_stream_node_addr_t           *a_stream_addr,
    uint8_t                          a_ch_id,
    dap_stream_packet_direction_t    a_direction,
    dap_stream_ch_notify_callback_t  a_callback,
    void                             *a_callback_arg
);
```

### Направления

```c
typedef enum dap_stream_packet_direction {
    DAP_STREAM_PKT_DIR_IN,   // Входящие пакеты
    DAP_STREAM_PKT_DIR_OUT   // Исходящие пакеты
} dap_stream_packet_direction_t;
```

### Механизм работы

1. Нотификаторы хранятся в двух связных списках (`dap_list_t`) на канале: `packet_in_notifiers` и `packet_out_notifiers`
2. Регистрация/удаление выполняется асинхронно через `dap_worker_exec_callback_on()` — на целевом воркере
3. Дубликаты предотвращаются через `dap_list_find()` с функцией сравнения `s_notifiers_compare()` (сравнение по `callback` + `arg`)
4. При отправке пакета (`dap_stream_ch_pkt_write_unsafe`) итерируется `packet_out_notifiers`, при приёме — `packet_in_notifiers`

### Защита от use-after-free

Итерация нотификаторов проверяет флаг `closing`:

```c
for (dap_list_t *it = a_ch->packet_out_notifiers; !a_ch->closing && it; it = it->next) {
    dap_stream_ch_notifier_t *l_notifier = it->data;
    l_notifier->callback(a_ch, a_type, a_data, a_data_size, l_notifier->arg);
}
```

Если канал помечен как `closing`, итерация прерывается. Отложенное освобождение через `dap_worker_exec_callback_on()` гарантирует, что память канала не будет освобождена до завершения текущей итерации.

## Вспомогательные функции

### Поиск канала по UUID

```c
dap_stream_ch_t *dap_stream_ch_find_by_uuid_unsafe(
    dap_stream_worker_t  *a_worker, // Воркер (UNSAFE: только в контексте worker)
    dap_stream_ch_uuid_t a_uuid     // UUID канала
);
```

Поиск в хеш-таблице воркера с блокировкой `channels_rwlock` на чтение.

### Поиск канала по ID

```c
dap_stream_ch_t *dap_stream_ch_by_id_unsafe(
    dap_stream_t *a_stream, // Поток
    const char    a_ch_id   // Символьный ID (напр. 'S')
);
```

Линейный поиск по массиву каналов потока. Сравнивает `channel[i]->proc->id`.

### Проверка UUID (MT-safe)

```c
DAP_STATIC_INLINE bool dap_stream_ch_check_uuid_mt(
    dap_stream_worker_t  *a_worker,  // Воркер
    dap_stream_ch_uuid_t a_ch_uuid   // UUID канала
);
```

Возвращает `true`, если канал с данным UUID существует в указанном воркере.

### Получение воркера по UUID канала

```c
dap_worker_t *dap_stream_ch_get_worker_mt(dap_stream_ch_uuid_t a_ch_uuid);
```

Поиск воркера, владеющего каналом с данным UUID. Перебирает всех воркеров с блокировкой `channels_rwlock`. Полностью потокобезопасна (MT-safe).

### Управление готовностью

```c
void dap_stream_ch_set_ready_to_read_unsafe(dap_stream_ch_t *a_ch, bool a_is_ready);
void dap_stream_ch_set_ready_to_write_unsafe(dap_stream_ch_t *a_ch, bool a_is_ready);
```

Устанавливают флаги `ready_to_read`/`ready_to_write` и синхронизируют состояние с esocket через `dap_events_socket_set_readable_unsafe()` / `dap_events_socket_set_writable_unsafe()`.

## Кэшет канала

```c
typedef struct dap_stream_ch_cachet {
    dap_stream_worker_t  *stream_worker; // Воркер
    dap_stream_ch_uuid_t uuid;          // UUID канала
} dap_stream_ch_cachet_t;
```

Лёгкая структура для кэширования ссылки на канал (воркер + UUID) без хранения полного указателя на `dap_stream_ch_t`.

## Ключевые константы

| Константа | Значение | Описание |
|-----------|----------|----------|
| `TECHICAL_CHANNEL_ID` | `'t'` | ID технического (служебного) канала |
| `STREAM_CH_PKT_TYPE_REQUEST` | `0x0` | Тип пакета «запрос» (по умолчанию) |
| `DAP_STREAM_PKT_ENCRYPTION_OVERHEAD` | 200 bytes | Overhead шифрования (из stream_pkt) |
| `DAP_STREAM_PKT_FRAGMENT_SIZE` | (из stream_pkt) | Максимальный размер пакета |

## Связанные документы

- [01 — Stream Protocol](01-stream-protocol_ru.md) — ядро потокового протокола
- [03 — DSHP Handshake](03-handshake_ru.md) — установка соединения
- [01 — Transport Abstraction](../transport/02-transport-abstraction_ru.md) — абстракция транспорта
- Заголовочные файлы: `ch/include/dap_stream_ch.h`, `dap_stream_ch_proc.h`, `dap_stream_ch_pkt.h`
