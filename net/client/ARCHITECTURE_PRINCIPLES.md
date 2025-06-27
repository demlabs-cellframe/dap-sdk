# ARCHITECTURE PRINCIPLES - НЕ НАРУШАТЬ!

## 🚨 КРИТИЧЕСКИЕ ПРИНЦИПЫ

### 1. **НЕ ИЗОБРЕТАТЬ ВЕЛОСИПЕД**
- Session/Stream API УЖЕ спроектированы как УНИВЕРСАЛЬНЫЕ
- НЕ создавать новые функции типа `create_and_connect` 
- ИСПОЛЬЗОВАТЬ существующие `dap_http2_session_create()` + `dap_http2_session_connect()`
- АДАПТИРОВАТЬ логику старого модуля к новой архитектуре, а НЕ наоборот

### 2. **УНИВЕРСАЛЬНОСТЬ СЛОЁВ**
- **Session Layer**: универсальный для ВСЕХ клиентов (HTTP, WebSocket, Binary, etc.)
- **Stream Layer**: универсальный для ВСЕХ протоколов приложения
- **Client Layer**: специфичный только для HTTP клиента

### 3. **ДЕКОМПОЗИЦИЯ**
- Session: `create()` → `connect()` → `send()` → `receive()`
- Stream: `create()` → `set_callback()` → `process_data()`
- Client: `create()` → `request()` → `callbacks`

### 4. **СВЕРХЭФФЕКТИВНЫЙ КОД** 🚀
#### 4.1. Минимизация выделений памяти
- **Enum вместо строк**: HTTP методы как enum с O(1) преобразованием в строки
- **NULL-опциональные парсеры**: принимают NULL для ненужных параметров
- **Единственное выделение**: вместо множественных malloc/free
- **Переиспользование буферов**: где возможно

#### 4.2. Эффективные алгоритмы
- **Ранний выход**: проверка первого символа перед полным сравнением строк
- **Единственный проход**: парсинг URL за один цикл вместо множественных strchr()
- **Ручной парсинг чисел**: для коротких чисел быстрее strtol()
- **Массивы вместо switch**: для преобразования enum → string

#### 4.3. Принцип "экономим на копейках"
```c
// ❌ ПЛОХО: лишние выделения
char *method = dap_strdup("GET");

// ✅ ХОРОШО: enum без выделений  
dap_http_method_t method = DAP_HTTP_METHOD_GET;
const char *method_str = dap_http_method_to_string(method); // O(1)
```

```c
// ❌ ПЛОХО: выделяем память для ненужного path
s_parse_url(url, &host, &port, &path, &ssl);
DAP_DELETE(path); // Сразу удаляем!

// ✅ ХОРОШО: не выделяем ненужное
s_parse_url_efficient(url, &host, &port, NULL, &ssl); // path = NULL
```

#### 4.4. Разумные оптимизации
```c
// ✅ ХОРОШО: читаемо и эффективно
if (strncasecmp(url, "http://", 7) == 0)

// ✅ ХОРОШО: ранний выход по первому символу
switch (method_str[0]) {
    case 'G': if (strcmp(method_str, "GET") == 0) return DAP_HTTP_METHOD_GET;
    case 'P': if (strcmp(method_str, "POST") == 0) return DAP_HTTP_METHOD_POST;
}
```

### 5. **БЕЗОПАСНОСТЬ БЕЗ ИЗБЫТОЧНОСТИ**
- Проверки входных данных **только в точках мутации**
- НЕ проверять buf corruption в pop/consume функциях
- Валидация **только там, где ошибки могут возникнуть**

### 6. **ПРАВИЛА КОДА**
- Комментарии функций на английском
- Именование: `l_`, `a_`, `s_` префиксы
- `DAP_NEW`, `DAP_DELETE`, `DAP_FREE` макросы
- Временные переменные при realloc
- Минимизация глобальных переменных

## 🎯 ПРАВИЛЬНЫЙ ПОДХОД

### Вместо создания `session_create_and_connect()`:
```c
// НЕПРАВИЛЬНО - новая функция
dap_http2_session_t* dap_http2_session_create_and_connect(host, port, ssl);

// ПРАВИЛЬНО - использовать существующие
session = dap_http2_session_create(worker, timeout);
dap_http2_session_connect(session, host, port, ssl);
```

### Вместо дублирования Session функций в Client:
```c
// НЕПРАВИЛЬНО - дублировать в Client
dap_http2_client_connect(client, host, port);

// ПРАВИЛЬНО - Client использует Session API
client->session = dap_http2_session_create(worker, timeout);
dap_http2_session_connect(client->session, host, port, ssl);
```

### Вместо HTTP-специфичных Session функций:
```c
// НЕПРАВИЛЬНО - HTTP логика в Session
dap_http2_session_send_http_request(session, request);

// ПРАВИЛЬНО - HTTP логика в Stream
stream = dap_http2_session_create_stream(session);
dap_http2_stream_set_http_client_mode(stream);
dap_http2_stream_send_request(stream, http_request);
```

## 🔄 ПРАВИЛЬНЫЙ FLOW

### HTTP Client Request Flow:
1. **Client Layer**: 
   - Парсит URL (host, port, path)
   - Создаёт Session через универсальный API
   - Создаёт Stream через Session API
   - Настраивает Stream в HTTP режим

2. **Stream Layer**:
   - Форматирует HTTP запрос
   - Отправляет через session->send()
   - Парсит HTTP ответ в read_callback
   - Уведомляет Client через callbacks

3. **Session Layer**:
   - Управляет TCP/SSL соединением
   - Передаёт raw данные в Stream
   - Обрабатывает таймауты подключения

## 📋 ЗАДАЧА: АДАПТАЦИЯ, НЕ ИЗОБРЕТЕНИЕ

Наша задача - взять логику из `dap_client_http.c` и **РАЗЛОЖИТЬ** её по правильным слоям:

- **Парсинг URL** → Client Layer
- **TCP connect + SSL** → Session Layer  
- **HTTP formatting + parsing** → Stream Layer
- **Streaming decision** → Stream Layer
- **Chunked processing** → Stream Layer
- **Redirects** → Client Layer (создаёт новую Session)
- **Timeouts** → Session Layer (connect) + Stream Layer (read)

## ⚠️ ЧТО НЕ ДЕЛАТЬ

1. ❌ Создавать `session_create_and_connect()`
2. ❌ Дублировать Session API в Client
3. ❌ Добавлять HTTP логику в Session
4. ❌ Изменять уже утверждённые сигнатуры
5. ❌ Создавать HTTP-специфичные Session функции

## ✅ ЧТО ДЕЛАТЬ

1. ✅ Использовать существующий Session/Stream API
2. ✅ Адаптировать старую логику к новым слоям
3. ✅ Сохранять универсальность Session/Stream
4. ✅ HTTP-специфику только в Client + Stream callbacks
5. ✅ Следовать принципу разделения ответственности 

## 🎯 **РЕЗУЛЬТАТ**
Код должен быть:
- ⚡ **Быстрым**: O(1) операции где возможно
- 💾 **Экономным**: минимум malloc/free
- 🎯 **Точным**: только нужные вычисления
- 🔧 **Простым**: читаемым и сопровождаемым 