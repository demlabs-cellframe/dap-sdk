# HTTP2 Client State Machines Design

## Архитектура состояний

Трехслойная архитектура HTTP2 клиента использует независимые стейт-машины для каждого слоя с четкими правилами взаимодействия.

## 1. Session Layer State Machine (Нижний слой)

### Состояния:
- **IDLE** - Начальное состояние, сокет не создан
- **CONNECTING** - Сокет создан, идет подключение, активен таймер подключения  
- **CONNECTED** - Сокет подключен, готов к передаче данных, активен таймер чтения
- **CLOSING** - Корректное закрытие, ожидание закрытия сокета
- **ERROR** - Ошибка сети/таймаута, идет очистка ресурсов
- **CLOSED** - Ресурсы очищены, готов к удалению

### Переходы:
```
IDLE → CONNECTING: dap_http2_session_connect()
CONNECTING → CONNECTED: socket connected callback
CONNECTING → ERROR: connect timeout / network error
CONNECTED → CLOSING: dap_http2_session_close() / error
CONNECTED → ERROR: read timeout / network error  
CLOSING → CLOSED: socket closed callback
ERROR → CLOSED: cleanup completed
```

### Ответственность:
- Управление сокетами и соединениями
- Обработка таймеров (подключение, чтение активности)
- SSL/TLS рукопожатие
- Низкоуровневые сетевые ошибки

## 2. Stream Layer State Machine (Средний слой)

### Состояния:
- **IDLE** - Стрим создан, ожидает запрос
- **REQUEST_SENT** - HTTP запрос отправлен, ожидает ответ, буфер пуст
- **HEADERS** - Парсинг HTTP заголовков, определение режима ответа
- **BODY** - Обработка тела ответа в выбранном режиме
- **COMPLETE** - Ответ полностью получен, вызван финальный callback
- **ERROR** - Ошибка парсинга/буфера, требуется очистка

### Переходы:
```
IDLE → REQUEST_SENT: send HTTP request
REQUEST_SENT → HEADERS: first data received
REQUEST_SENT → ERROR: session error/timeout
HEADERS → BODY: headers parsed successfully
HEADERS → COMPLETE: headers-only response (HEAD/304/etc)
HEADERS → ERROR: parse error
BODY → BODY: data chunk received (accumulate/stream)
BODY → COMPLETE: all data received
BODY → ERROR: parse error/buffer overflow
```

### Режимы обработки тела (в состоянии BODY):
- **ACCUMULATE** - Накопление в буфере до получения всех данных
- **STREAMING** - Zero-copy передача данных через callback
- **CHUNKED** - Обработка Transfer-Encoding: chunked

### Ответственность:
- HTTP парсинг заголовков и тела
- Определение режима обработки ответа
- Управление буферами приема
- Обработка Transfer-Encoding

## 3. Client Layer State Machine (Верхний слой)

### Состояния:
- **IDLE** - Клиент создан, готов к запросам, нет активной сессии
- **REQUESTING** - Запрос инициирован, сессия подключается, стрим создается  
- **RECEIVING** - Подключен и получает данные, стрим обрабатывает данные
- **COMPLETE** - Запрос завершен, ответ доставлен, статистика обновлена
- **ERROR** - Произошла ошибка, вызван error callback, идет очистка
- **CANCELLED** - Пользователь отменил запрос, идет очистка, callback не вызывается

### Переходы:
```
IDLE → REQUESTING: dap_http2_client_request_async/sync()
REQUESTING → RECEIVING: session connected + stream headers parsed
REQUESTING → ERROR: session error/connect fail
REQUESTING → CANCELLED: dap_http2_client_cancel()
RECEIVING → RECEIVING: stream data chunk received
RECEIVING → COMPLETE: stream complete
RECEIVING → ERROR: stream error/timeout
RECEIVING → CANCELLED: dap_http2_client_cancel()
COMPLETE → IDLE: new request (reuse client)
ERROR → IDLE: reset/retry
CANCELLED → IDLE: reset
```

### Ответственность:
- Публичный API и управление запросами
- Конфигурация и callback интерфейсы
- Статистика и мониторинг
- Координация между session и stream

## Взаимодействие между слоями

### Правила взаимодействия:

1. **Client → Session**:
   - Client создает Session при необходимости
   - Client может принудительно закрыть Session
   - Session уведомляет Client об изменениях состояния

2. **Client → Stream**: 
   - Client создает Stream после подключения Session
   - Client настраивает режим обработки Stream
   - Stream уведомляет Client о данных и завершении

3. **Stream → Session**:
   - Stream использует Session только для отправки данных
   - Stream не управляет жизненным циклом Session
   - Session передает полученные данные в Stream

### Соответствие состояний:

| Client State | Stream State | Session State | Описание |
|--------------|--------------|---------------|----------|
| IDLE | IDLE | IDLE | Начальное состояние |
| REQUESTING | REQUEST_SENT | CONNECTING/CONNECTED | Установка соединения |
| RECEIVING | HEADERS/BODY | CONNECTED | Активная передача данных |
| COMPLETE | COMPLETE | CONNECTED/CLOSING | Завершение запроса |
| ERROR | ERROR | ERROR | Обработка ошибок |
| CANCELLED | ERROR | CLOSING | Отмена пользователем |

## Обработка ошибок

### Типы ошибок и их распространение:

1. **Connection Timeout** (Session → Client):
   ```
   Session: CONNECTING → ERROR
   Client: REQUESTING → ERROR
   ```

2. **Read Timeout** (Session → Stream → Client):
   ```
   Session: CONNECTED → ERROR
   Stream: HEADERS/BODY → ERROR  
   Client: RECEIVING → ERROR
   ```

3. **Parse Error** (Stream → Client):
   ```
   Stream: HEADERS/BODY → ERROR
   Client: RECEIVING → ERROR
   Session: остается CONNECTED (может быть переиспользована)
   ```

4. **Network Error** (Session → Stream → Client):
   ```
   Session: CONNECTED → ERROR
   Stream: любое → ERROR
   Client: любое → ERROR
   ```

5. **User Cancellation** (Client → Stream → Session):
   ```
   Client: любое → CANCELLED
   Stream: любое → ERROR (cleanup)
   Session: любое → CLOSING
   ```

## Принципы реализации

1. **Автономность слоев** - каждый слой управляет своим состоянием независимо
2. **Однонаправленные уведомления** - нижние слои уведомляют верхние о событиях
3. **Четкие границы ответственности** - каждый слой отвечает только за свою функциональность
4. **Graceful degradation** - ошибки на одном слое не должны крашить другие слои
5. **Возможность переиспользования** - Session может обслуживать несколько Stream (в будущем)

## Таймеры и их влияние на состояния

### Session Layer Timers:
- **Connect Timer**: CONNECTING → ERROR при истечении
- **Read Timer**: CONNECTED → ERROR при отсутствии активности

### Client Layer Timers:  
- **Total Request Timer**: любое состояние → ERROR при истечении общего лимита времени

### Приоритеты таймеров:
1. Connect timeout (сессия)
2. Read timeout (сессия) 
3. Total timeout (клиент)

Эта архитектура обеспечивает четкое разделение ответственности, надежную обработку ошибок и возможность расширения функциональности в будущем. 