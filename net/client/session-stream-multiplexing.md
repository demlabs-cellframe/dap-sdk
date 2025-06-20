# Сессия как проводная линия: мультиплексирование стримов

## 📡 Аналогия с проводной линией

### 🔌 **Физическая аналогия:**
```
Проводная линия (Session)     = TCP соединение
├── Канал 1 (Stream)         = HTTP запрос/ответ  
├── Канал 2 (Stream)         = WebSocket поток
├── Канал 3 (Stream)         = SSE события
└── Канал N (Stream)         = Новый HTTP запрос
```

**Принцип:** Одна физическая линия (TCP сокет) передает данные множественных логических каналов (HTTP стримов), каждый с собственным идентификатором.

### 🌊 **Поток данных в мультиплексированной сессии:**
```c
TCP Socket buf_in:
[Stream_ID=1][HTTP Headers...]
[Stream_ID=3][WebSocket Frame...]  
[Stream_ID=1][HTTP Body chunk...]
[Stream_ID=5][SSE Event...]
[Stream_ID=1][HTTP Body end]
```

## 🔄 Жизненный цикл сессии после отвязки стримов

### 📊 **Состояния сессии:**
```c
typedef enum {
    DAP_HTTP2_SESSION_IDLE,           // Нет активных стримов
    DAP_HTTP2_SESSION_ACTIVE,         // Есть активные стримы (любые: HTTP, WebSocket, SSE)
    DAP_HTTP2_SESSION_CLOSING,        // Закрытие по инициативе пользователя
    DAP_HTTP2_SESSION_CLOSED          // Закрыта
} dap_http2_session_state_t;
```

### 🔄 **Сценарии после отвязки стрима:**

#### **Сценарий 1: HTTP Client отвязывается, WebSocket остается**
```c
// Начальное состояние
Session: ACTIVE
├── Stream 1: HTTP (Client A attached)
└── Stream 3: WebSocket (Client B attached)

// Client A завершает HTTP запрос и отвязывается
dap_http2_client_detach_stream(client_A); 

// Результат
Session: ACTIVE (1 стрим)
└── Stream 3: WebSocket (автономный, в Stream Manager)

// Session готова принять новые HTTP запросы!
```

#### **Сценарий 2: Все клиенты отвязались, остались автономные стримы**
```c
// Состояние после отвязки всех HTTP клиентов
Session: ACTIVE (3 стрима)
├── Stream 3: WebSocket (автономный)
├── Stream 5: SSE (автономный)  
└── Stream 7: Long polling (автономный)

// Session НЕ закрывается, продолжает обслуживать автономные стримы
// Новые HTTP клиенты могут подключиться к этой же сессии
```

#### **Сценарий 3: Динамическое добавление новых стримов**
```c
// К существующей сессии с автономными стримами
Session: ACTIVE (1 стрим: Stream 3: WebSocket)

// Новый HTTP клиент хочет использовать ту же сессию
dap_http2_client_t *new_client = dap_http2_client_create();
dap_http2_client_use_session(new_client, existing_session);
dap_http2_client_get_async(new_client, "/api/data", callback, arg);

// Результат
Session: ACTIVE (2 стрима)
├── Stream 3: WebSocket (автономный)
└── Stream 9: HTTP GET (Client attached)
```

## 🎯 Минималистичное API для динамического управления

### 🔧 **Core Session API:**
```c
/**
 * @brief Создать сессию для хоста
 * @param a_host Хост (например, "api.example.com")
 * @param a_port Порт
 * @param a_is_ssl Использовать SSL
 * @return Указатель на сессию или NULL при ошибке
 */
dap_http2_session_t *dap_http2_session_create(const char *a_host, uint16_t a_port, bool a_is_ssl);

/**
 * @brief Найти существующую сессию для хоста
 * @param a_host Хост
 * @param a_port Порт  
 * @param a_is_ssl SSL флаг
 * @return Указатель на существующую сессию или NULL
 */
dap_http2_session_t *dap_http2_session_find(const char *a_host, uint16_t a_port, bool a_is_ssl);

/**
 * @brief Получить или создать сессию (connection pooling)
 * @param a_host Хост
 * @param a_port Порт
 * @param a_is_ssl SSL флаг
 * @return Указатель на сессию (существующую или новую)
 */
dap_http2_session_t *dap_http2_session_get_or_create(const char *a_host, uint16_t a_port, bool a_is_ssl);

/**
 * @brief Проверить, может ли сессия принять новые стримы
 * @param a_session Сессия
 * @return true если может принимать новые стримы
 */
bool dap_http2_session_can_accept_streams(dap_http2_session_t *a_session);

/**
 * @brief Получить количество активных стримов
 * @param a_session Сессия
 * @return Количество стримов
 */
size_t dap_http2_session_get_stream_count(dap_http2_session_t *a_session);
```

### 🌊 **Stream Management API:**
```c
/**
 * @brief Создать новый стрим на существующей сессии
 * @param a_session Сессия
 * @param a_stream_id ID стрима (0 = автоматический)
 * @return Указатель на стрим или NULL при ошибке
 */
dap_http2_stream_t *dap_http2_stream_create_on_session(dap_http2_session_t *a_session, uint32_t a_stream_id);

/**
 * @brief Отвязать стрим от клиента (сделать автономным)
 * @param a_stream Стрим
 * @return 0 при успехе, отрицательное значение при ошибке
 */
int dap_http2_stream_detach(dap_http2_stream_t *a_stream);

/**
 * @brief Привязать автономный стрим к новому клиенту
 * @param a_client Клиент
 * @param a_stream Автономный стрим
 * @return 0 при успехе, отрицательное значение при ошибке
 */
int dap_http2_stream_attach_to_client(dap_http2_client_t *a_client, dap_http2_stream_t *a_stream);

/**
 * @brief Закрыть стрим (удалить из сессии)
 * @param a_stream Стрим
 */
void dap_http2_stream_close(dap_http2_stream_t *a_stream);
```

### 🔗 **Client Integration API:**
```c
/**
 * @brief Использовать существующую сессию для клиента
 * @param a_client Клиент
 * @param a_session Существующая сессия
 * @return 0 при успехе, отрицательное значение при ошибке
 */
int dap_http2_client_use_session(dap_http2_client_t *a_client, dap_http2_session_t *a_session);

/**
 * @brief Автоматически найти или создать сессию для клиента
 * @param a_client Клиент
 * @param a_host Хост
 * @param a_port Порт
 * @param a_is_ssl SSL флаг
 * @return 0 при успехе, отрицательное значение при ошибке
 */
int dap_http2_client_auto_session(dap_http2_client_t *a_client, const char *a_host, uint16_t a_port, bool a_is_ssl);

/**
 * @brief Отвязать клиента от стрима (стрим становится автономным)
 * @param a_client Клиент
 * @return 0 при успехе, отрицательное значение при ошибке
 */
int dap_http2_client_detach_stream(dap_http2_client_t *a_client);
```

## 🎪 Практические сценарии использования

### 🌐 **1. Веб-браузер (множественные ресурсы):**
```c
// Создаем или находим сессию для домена
dap_http2_session_t *session = dap_http2_session_get_or_create("example.com", 443, true);

// Загружаем множественные ресурсы параллельно
dap_http2_client_t *html_client = dap_http2_client_create();
dap_http2_client_use_session(html_client, session);
dap_http2_client_get_async(html_client, "/index.html", html_callback, NULL);

dap_http2_client_t *css_client = dap_http2_client_create();  
dap_http2_client_use_session(css_client, session);
dap_http2_client_get_async(css_client, "/style.css", css_callback, NULL);

dap_http2_client_t *js_client = dap_http2_client_create();
dap_http2_client_use_session(js_client, session);
dap_http2_client_get_async(js_client, "/script.js", js_callback, NULL);

// Все запросы идут по одному TCP соединению!
```

### 🔄 **2. API клиент с переиспользованием соединения:**
```c
// Создаем сессию один раз
dap_http2_session_t *api_session = dap_http2_session_create("api.service.com", 443, true);

// Функция для API вызовов
void make_api_call(const char *endpoint, callback_t callback, void *arg) {
    dap_http2_client_t *client = dap_http2_client_create();
    dap_http2_client_use_session(client, api_session); // Переиспользуем сессию
    dap_http2_client_get_async(client, endpoint, callback, arg);
    // После завершения запроса клиент удаляется, сессия остается
}

// Множественные вызовы
make_api_call("/users", users_callback, NULL);
make_api_call("/posts", posts_callback, NULL);  
make_api_call("/comments", comments_callback, NULL);
```

### 🌊 **3. Смешанный режим: HTTP + WebSocket:**
```c
// Создаем сессию
dap_http2_session_t *session = dap_http2_session_create("chat.example.com", 443, true);

// Обычный HTTP запрос для аутентификации
dap_http2_client_t *auth_client = dap_http2_client_create();
dap_http2_client_use_session(auth_client, session);
dap_http2_client_post_async(auth_client, "/auth", credentials, auth_callback, NULL);

// WebSocket для чата (после аутентификации)
void auth_callback(dap_http2_client_t *client, http_status_code_t code, const void *data, size_t size) {
    if (code == HTTP_STATUS_OK) {
        // Создаем WebSocket на той же сессии
        dap_http2_client_t *ws_client = dap_http2_client_create();
        dap_http2_client_use_session(ws_client, session);
        dap_http2_client_websocket_upgrade(ws_client, "/chat", ws_callback, NULL);
        
        // После upgrade WebSocket становится автономным
        // Сессия может принимать новые HTTP запросы
    }
}
```

### 🔧 **4. Динамическое добавление стримов:**
```c
// У нас есть сессия с автономным WebSocket
dap_http2_session_t *session = existing_websocket_session;

// Проверяем, может ли сессия принять новые стримы
if (dap_http2_session_can_accept_streams(session)) {
    // Добавляем новый HTTP запрос к существующей сессии
    dap_http2_client_t *new_client = dap_http2_client_create();
    dap_http2_client_use_session(new_client, session);
    dap_http2_client_get_async(new_client, "/status", status_callback, NULL);
    
    log_it(L_INFO, "Added new stream to session with %zu existing streams", 
           dap_http2_session_get_stream_count(session));
}
```

## 🏗️ Внутренняя структура сессии

### 📊 **Расширенная структура сессии:**
```c
typedef struct dap_http2_session {
    // Сетевое соединение
    dap_events_socket_t *es;
    dap_worker_t *worker;
    
    // Информация о хосте
    char host[DAP_HOSTADDR_STRLEN];
    uint16_t port;
    bool is_ssl;
    
    // Управление стримами
    dap_http2_stream_t **streams;        // Массив указателей на стримы
    size_t streams_count;                // Количество активных стримов
    size_t streams_capacity;             // Емкость массива
    uint32_t next_stream_id;             // Следующий ID для нового стрима
    
    // Счетчики
    size_t attached_streams;             // Стримы с привязанными клиентами
    size_t autonomous_streams;           // Автономные стримы
    
    // Состояние
    dap_http2_session_state_t state;
    time_t ts_last_activity;
    
    // Callback'и
    dap_http2_session_callbacks_t callbacks;
} dap_http2_session_t;
```

### 🔄 **Логика мультиплексирования:**
```c
// Обработка входящих данных
static void s_session_data_received(dap_events_socket_t *a_es, void *a_arg) {
    dap_http2_session_t *l_session = (dap_http2_session_t *)a_arg;
    
    // Читаем данные из сокета
    size_t l_data_size = dap_events_socket_pop_from_buf_in(a_es, 
                                                           l_buffer, 
                                                           sizeof(l_buffer));
    
    // Определяем, какому стриму принадлежат данные
    uint32_t l_stream_id = s_extract_stream_id(l_buffer, l_data_size);
    
    // Находим соответствующий стрим
    dap_http2_stream_t *l_stream = s_session_find_stream(l_session, l_stream_id);
    if (l_stream) {
        // Передаем данные стриму для обработки
        dap_http2_stream_process_data(l_stream, l_buffer, l_data_size);
    }
}
```

## ✅ Ответы на вопросы

### ❓ **"Возможно ли динамически добавлять новые стримы?"**
**✅ Да, это основная фича архитектуры:**

1. **Сессия остается живой** после отвязки клиентов
2. **Новые стримы** можно создавать на существующей сессии
3. **Connection pooling** автоматически переиспользует сессии
4. **API минималистичен** - всего 3-4 функции для полного управления

### ❓ **"Как это работает с аналогией проводной линии?"**
**✅ Точно как телефонная станция:**

- **Физическая линия (TCP)** = одна на всех
- **Логические каналы (Streams)** = множество, каждый со своим ID
- **Коммутация** = сессия направляет данные нужному стриму
- **Динамическое подключение** = новые каналы можно добавлять/удалять

Эта архитектура обеспечивает максимальную эффективность использования сетевых ресурсов при сохранении простоты API. 