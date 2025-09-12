#!/bin/bash

# Скрипт запуска МЦП серверов для DAP SDK
# Автор: DAP SDK Team
# Версия: 1.0

set -e

# Цвета для вывода
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Функция для логирования
log() {
    echo -e "${BLUE}[$(date +'%Y-%m-%d %H:%M:%S')]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[$(date +'%Y-%m-%d %H:%M:%S')] SUCCESS:${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[$(date +'%Y-%m-%d %H:%M:%S')] WARNING:${NC} $1"
}

log_error() {
    echo -e "${RED}[$(date +'%Y-%m-%d %H:%M:%S')] ERROR:${NC} $1"
}

# Конфигурация
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$(dirname "$SCRIPT_DIR")")"
BUILD_DIR="${PROJECT_ROOT}/build"
BIN_DIR="${BUILD_DIR}/bin"
CONFIG_DIR="${PROJECT_ROOT}/config"
LOG_DIR="${PROJECT_ROOT}/logs"
PID_DIR="${PROJECT_ROOT}/pids"

# Создание необходимых директорий
mkdir -p "$LOG_DIR" "$PID_DIR" "$CONFIG_DIR"

# Функция для проверки зависимостей
check_dependencies() {
    log "Проверка зависимостей..."
    
    # Проверка наличия скомпилированных бинарников
    if [ ! -d "$BIN_DIR" ]; then
        log_error "Директория с бинарниками не найдена: $BIN_DIR"
        log "Выполните сборку проекта: mkdir build && cd build && cmake .. && make"
        exit 1
    fi
    
    # Проверка наличия конфигурационных файлов
    if [ ! -f "$CONFIG_DIR/server.conf" ]; then
        log_warning "Конфигурационный файл не найден: $CONFIG_DIR/server.conf"
        log "Создание базового конфигурационного файла..."
        create_default_config
    fi
    
    log_success "Зависимости проверены"
}

# Создание базового конфигурационного файла
create_default_config() {
    cat > "$CONFIG_DIR/server.conf" << 'EOF'
# Конфигурация DAP SDK серверов

# Общие настройки
[general]
log_level = info
log_file = logs/dap_server.log
pid_file = pids/dap_server.pid

# HTTP сервер
[http_server]
enabled = true
host = 0.0.0.0
port = 8080
max_connections = 1000
timeout = 30

# JSON-RPC сервер
[json_rpc_server]
enabled = true
host = 0.0.0.0
port = 8081
max_connections = 100
timeout = 60

# DNS сервер
[dns_server]
enabled = true
host = 0.0.0.0
port = 53
upstream_dns = 8.8.8.8

# Encryption сервер
[encryption_server]
enabled = true
host = 0.0.0.0
port = 8082
key_size = 256

# Notification сервер
[notification_server]
enabled = true
host = 0.0.0.0
port = 8083
max_subscribers = 10000

# База данных
[database]
type = mdbx
path = data/dap.db
max_size = 1073741824  # 1GB
EOF
    
    log_success "Создан базовый конфигурационный файл: $CONFIG_DIR/server.conf"
}

# Функция для запуска сервера
start_server() {
    local server_name="$1"
    local binary_name="$2"
    local config_section="$3"
    local port="$4"
    
    log "Запуск $server_name..."
    
    # Проверка наличия бинарника
    local binary_path="$BIN_DIR/$binary_name"
    if [ ! -f "$binary_path" ]; then
        log_error "Бинарник не найден: $binary_path"
        return 1
    fi
    
    # Проверка, не запущен ли уже сервер
    local pid_file="$PID_DIR/${server_name}.pid"
    if [ -f "$pid_file" ]; then
        local pid=$(cat "$pid_file")
        if kill -0 "$pid" 2>/dev/null; then
            log_warning "$server_name уже запущен (PID: $pid)"
            return 0
        else
            log "Удаление устаревшего PID файла: $pid_file"
            rm -f "$pid_file"
        fi
    fi
    
    # Запуск сервера
    local log_file="$LOG_DIR/${server_name}.log"
    nohup "$binary_path" \
        --config "$CONFIG_DIR/server.conf" \
        --section "$config_section" \
        --log-file "$log_file" \
        --pid-file "$pid_file" \
        > "$log_file" 2>&1 &
    
    local server_pid=$!
    echo "$server_pid" > "$pid_file"
    
    # Ожидание запуска
    sleep 2
    
    # Проверка, что сервер запустился
    if kill -0 "$server_pid" 2>/dev/null; then
        log_success "$server_name запущен (PID: $server_pid, Port: $port)"
        return 0
    else
        log_error "Не удалось запустить $server_name"
        return 1
    fi
}

# Функция для остановки сервера
stop_server() {
    local server_name="$1"
    local pid_file="$PID_DIR/${server_name}.pid"
    
    if [ -f "$pid_file" ]; then
        local pid=$(cat "$pid_file")
        if kill -0 "$pid" 2>/dev/null; then
            log "Остановка $server_name (PID: $pid)..."
            kill -TERM "$pid"
            
            # Ожидание завершения
            local count=0
            while kill -0 "$pid" 2>/dev/null && [ $count -lt 10 ]; do
                sleep 1
                count=$((count + 1))
            done
            
            if kill -0 "$pid" 2>/dev/null; then
                log_warning "Принудительная остановка $server_name..."
                kill -KILL "$pid"
            fi
            
            rm -f "$pid_file"
            log_success "$server_name остановлен"
        else
            log_warning "$server_name не запущен"
            rm -f "$pid_file"
        fi
    else
        log_warning "PID файл не найден для $server_name"
    fi
}

# Функция для проверки статуса сервера
check_server_status() {
    local server_name="$1"
    local port="$2"
    local pid_file="$PID_DIR/${server_name}.pid"
    
    if [ -f "$pid_file" ]; then
        local pid=$(cat "$pid_file")
        if kill -0 "$pid" 2>/dev/null; then
            # Проверка доступности порта
            if netstat -ln 2>/dev/null | grep -q ":$port "; then
                log_success "$server_name работает (PID: $pid, Port: $port)"
                return 0
            else
                log_warning "$server_name запущен, но порт $port недоступен"
                return 1
            fi
        else
            log_error "$server_name не запущен (устаревший PID файл)"
            rm -f "$pid_file"
            return 1
        fi
    else
        log_error "$server_name не запущен"
        return 1
    fi
}

# Функция для отображения статуса всех серверов
show_status() {
    log "Статус серверов DAP SDK:"
    echo
    
    local servers=(
        "http_server:8080"
        "json_rpc_server:8081"
        "dns_server:53"
        "encryption_server:8082"
        "notification_server:8083"
    )
    
    for server_info in "${servers[@]}"; do
        local server_name=$(echo "$server_info" | cut -d: -f1)
        local port=$(echo "$server_info" | cut -d: -f2)
        check_server_status "$server_name" "$port"
    done
}

# Функция для отображения логов
show_logs() {
    local server_name="$1"
    local log_file="$LOG_DIR/${server_name}.log"
    
    if [ -f "$log_file" ]; then
        log "Логи $server_name:"
        tail -n 50 "$log_file"
    else
        log_error "Лог файл не найден: $log_file"
    fi
}

# Функция для отображения помощи
show_help() {
    echo "Использование: $0 [КОМАНДА] [СЕРВЕР]"
    echo
    echo "Команды:"
    echo "  start [сервер]    - Запуск сервера(ов)"
    echo "  stop [сервер]     - Остановка сервера(ов)"
    echo "  restart [сервер]  - Перезапуск сервера(ов)"
    echo "  status            - Показать статус всех серверов"
    echo "  logs [сервер]     - Показать логи сервера"
    echo "  help              - Показать эту справку"
    echo
    echo "Серверы:"
    echo "  http_server       - HTTP сервер (порт 8080)"
    echo "  json_rpc_server   - JSON-RPC сервер (порт 8081)"
    echo "  dns_server        - DNS сервер (порт 53)"
    echo "  encryption_server - Encryption сервер (порт 8082)"
    echo "  notification_server - Notification сервер (порт 8083)"
    echo "  all               - Все серверы"
    echo
    echo "Примеры:"
    echo "  $0 start all                    # Запустить все серверы"
    echo "  $0 start http_server            # Запустить только HTTP сервер"
    echo "  $0 stop json_rpc_server         # Остановить JSON-RPC сервер"
    echo "  $0 status                       # Показать статус всех серверов"
    echo "  $0 logs http_server             # Показать логи HTTP сервера"
}

# Основная функция
main() {
    local command="$1"
    local server="$2"
    
    case "$command" in
        "start")
            check_dependencies
            
            case "$server" in
                "http_server")
                    start_server "http_server" "dap_http_server" "http_server" "8080"
                    ;;
                "json_rpc_server")
                    start_server "json_rpc_server" "dap_json_rpc_server" "json_rpc_server" "8081"
                    ;;
                "dns_server")
                    start_server "dns_server" "dap_dns_server" "dns_server" "53"
                    ;;
                "encryption_server")
                    start_server "encryption_server" "dap_encryption_server" "encryption_server" "8082"
                    ;;
                "notification_server")
                    start_server "notification_server" "dap_notification_server" "notification_server" "8083"
                    ;;
                "all"|"")
                    start_server "http_server" "dap_http_server" "http_server" "8080"
                    start_server "json_rpc_server" "dap_json_rpc_server" "json_rpc_server" "8081"
                    start_server "dns_server" "dap_dns_server" "dns_server" "53"
                    start_server "encryption_server" "dap_encryption_server" "encryption_server" "8082"
                    start_server "notification_server" "dap_notification_server" "notification_server" "8083"
                    ;;
                *)
                    log_error "Неизвестный сервер: $server"
                    show_help
                    exit 1
                    ;;
            esac
            ;;
        "stop")
            case "$server" in
                "http_server")
                    stop_server "http_server"
                    ;;
                "json_rpc_server")
                    stop_server "json_rpc_server"
                    ;;
                "dns_server")
                    stop_server "dns_server"
                    ;;
                "encryption_server")
                    stop_server "encryption_server"
                    ;;
                "notification_server")
                    stop_server "notification_server"
                    ;;
                "all"|"")
                    stop_server "http_server"
                    stop_server "json_rpc_server"
                    stop_server "dns_server"
                    stop_server "encryption_server"
                    stop_server "notification_server"
                    ;;
                *)
                    log_error "Неизвестный сервер: $server"
                    show_help
                    exit 1
                    ;;
            esac
            ;;
        "restart")
            case "$server" in
                "http_server")
                    stop_server "http_server"
                    sleep 2
                    start_server "http_server" "dap_http_server" "http_server" "8080"
                    ;;
                "json_rpc_server")
                    stop_server "json_rpc_server"
                    sleep 2
                    start_server "json_rpc_server" "dap_json_rpc_server" "json_rpc_server" "8081"
                    ;;
                "dns_server")
                    stop_server "dns_server"
                    sleep 2
                    start_server "dns_server" "dap_dns_server" "dns_server" "53"
                    ;;
                "encryption_server")
                    stop_server "encryption_server"
                    sleep 2
                    start_server "encryption_server" "dap_encryption_server" "encryption_server" "8082"
                    ;;
                "notification_server")
                    stop_server "notification_server"
                    sleep 2
                    start_server "notification_server" "dap_notification_server" "notification_server" "8083"
                    ;;
                "all"|"")
                    stop_server "http_server"
                    stop_server "json_rpc_server"
                    stop_server "dns_server"
                    stop_server "encryption_server"
                    stop_server "notification_server"
                    sleep 2
                    start_server "http_server" "dap_http_server" "http_server" "8080"
                    start_server "json_rpc_server" "dap_json_rpc_server" "json_rpc_server" "8081"
                    start_server "dns_server" "dap_dns_server" "dns_server" "53"
                    start_server "encryption_server" "dap_encryption_server" "encryption_server" "8082"
                    start_server "notification_server" "dap_notification_server" "notification_server" "8083"
                    ;;
                *)
                    log_error "Неизвестный сервер: $server"
                    show_help
                    exit 1
                    ;;
            esac
            ;;
        "status")
            show_status
            ;;
        "logs")
            if [ -z "$server" ]; then
                log_error "Укажите имя сервера для просмотра логов"
                show_help
                exit 1
            fi
            show_logs "$server"
            ;;
        "help"|"-h"|"--help")
            show_help
            ;;
        "")
            log_error "Не указана команда"
            show_help
            exit 1
            ;;
        *)
            log_error "Неизвестная команда: $command"
            show_help
            exit 1
            ;;
    esac
}

# Обработка сигналов для корректного завершения
trap 'log "Получен сигнал завершения, остановка серверов..."; exit 0' INT TERM

# Запуск основной функции
main "$@"
