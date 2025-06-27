# Profile Flow Explanation

## Передача профиля по уровням

### 1. Client Level (верхний уровень)
```c
dap_http2_client_t *dap_http2_client_create_with_profile(dap_worker_t *a_worker,
                                                         const dap_stream_profile_t *a_profile) {
    // 1. Создаем Client
    dap_http2_client_t *l_client = dap_http2_client_create(a_worker);
    
    // 2. Создаем Session с session callbacks из профиля
    dap_http2_session_t *l_session = dap_http2_session_create(a_worker, 0);
    dap_http2_session_set_callbacks(l_session, &a_profile->session_callbacks, a_profile->callbacks_context);
    
    // 3. Создаем Stream с stream callbacks из профиля
    dap_http2_stream_t *l_stream = dap_http2_stream_create(l_session, 1);
    dap_http2_stream_set_read_callback(l_stream, a_profile->initial_read_callback, a_profile->callbacks_context);
    
    // 4. Связываем всё вместе
    dap_http2_session_set_stream(l_session, l_stream);
    // Client получит UID для этого stream
    l_client->stream_uid = generate_stream_uid(worker_id, stream_id);
    
    return l_client;
}
```

### 2. Session Level (транспортный уровень)
```c
// Session получает свои callbacks из профиля
void dap_http2_session_set_callbacks(dap_http2_session_t *a_session,
                                     const dap_http2_session_callbacks_t *a_callbacks,
                                     void *a_callbacks_arg) {
    a_session->callbacks = *a_callbacks;  // Копируем структуру
    a_session->callbacks_arg = a_callbacks_arg;
}

// Когда Session подключается, вызывается:
static void session_connected_internal(dap_http2_session_t *a_session) {
    if (a_session->callbacks.connected) {
        a_session->callbacks.connected(a_session);  // Вызов user callback из профиля
    }
}
```

### 3. Stream Level (прикладной уровень)
```c
// Stream получает свой entry point callback из профиля
void dap_http2_stream_set_read_callback(dap_http2_stream_t *a_stream,
                                       dap_stream_read_callback_t a_callback,
                                       void *a_context) {
    a_stream->read_callback = a_callback;
    a_stream->read_callback_context = a_context;
}

// Когда приходят данные, вызывается:
static void stream_process_data_internal(dap_http2_stream_t *a_stream, const void *a_data, size_t a_size) {
    if (a_stream->read_callback) {
        // Вызов user callback из профиля - здесь происходят embedded transitions!
        size_t l_processed = a_stream->read_callback(a_stream, a_data, a_size);
        // ... обработка результата
    }
}
```

## Схема передачи

```
User Code:
├─ dap_stream_profile_t profile = {
│   ├─ .session_callbacks = { .connected = my_session_connected }
│   ├─ .initial_read_callback = my_http_callback  // Entry point
│   └─ .callbacks_context = &my_context
│  }
│
├─ dap_http2_client_create_with_profile(worker, &profile)
│
└─ Внутри функции:
    ├─ Session получает: profile.session_callbacks + profile.callbacks_context
    ├─ Stream получает: profile.initial_read_callback + profile.callbacks_context
    └─ Client получает: UID для доступа к Stream

```

## Embedded Transitions в действии

```c
// User callback (entry point из профиля)
size_t my_http_callback(dap_http2_stream_t *a_stream, const void *a_data, size_t a_data_size) {
    my_context_t *l_ctx = (my_context_t *)dap_http2_stream_get_read_callback_context(a_stream);
    
    if (detect_websocket_upgrade(a_data, a_data_size)) {
        log_it(L_INFO, "HTTP → WebSocket transition");
        
        // EMBEDDED TRANSITION: переключаем callback прямо здесь
        dap_http2_stream_transition_protocol(a_stream, l_ctx->websocket_callback, l_ctx);
        
        return consume_http_headers(a_data, a_data_size);
    }
    
    return process_http_response(a_data, a_data_size);
}

// После transition следующие данные пойдут уже в websocket_callback
size_t my_websocket_callback(dap_http2_stream_t *a_stream, const void *a_data, size_t a_data_size) {
    // Обработка WebSocket frames
    return process_websocket_frame(a_data, a_data_size);
}
```

## Ключевые моменты

1. **Profile создается на стеке** - никаких malloc/free
2. **Callbacks копируются в Session/Stream** - profile может быть уничтожен после создания
3. **Context передается по ссылке** - один context для всех callbacks
4. **Embedded transitions** - происходят в user callbacks через `dap_http2_stream_transition_protocol()`
5. **Session и Stream** - не знают о существовании Profile, получают только свои callbacks 