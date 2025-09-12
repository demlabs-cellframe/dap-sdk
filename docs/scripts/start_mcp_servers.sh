#!/bin/bash

# Скрипт запуска MCP серверов для DAP SDK и CellFrame SDK
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
PROJECT_ROOT="$(dirname "$(dirname "$(dirname "$SCRIPT_DIR")")")"
DAP_SDK_ROOT="${PROJECT_ROOT}/dap-sdk"
CELLFRAME_SDK_ROOT="${PROJECT_ROOT}/cellframe-sdk"
LOG_DIR="${PROJECT_ROOT}/logs"
PID_DIR="${PROJECT_ROOT}/pids"

# Создание необходимых директорий
mkdir -p "$LOG_DIR" "$PID_DIR"

# Функция для проверки зависимостей
check_dependencies() {
    log "Проверка зависимостей..."

    # Проверка наличия Python
    if ! command -v python3 &> /dev/null; then
        log_error "Python 3 не найден. Установите Python 3."
        exit 1
    fi

    # Проверка наличия виртуального окружения и MCP SDK
    if [ ! -d "${PROJECT_ROOT}/mcp_env" ]; then
        log_warning "Виртуальное окружение не найдено. Создаем..."
        python3 -m venv "${PROJECT_ROOT}/mcp_env"
        source "${PROJECT_ROOT}/mcp_env/bin/activate"
        python3 -m pip install mcp || {
            log_error "Не удалось установить MCP SDK"
            exit 1
        }
    else
        source "${PROJECT_ROOT}/mcp_env/bin/activate"
        if ! python3 -c "import mcp" &> /dev/null; then
            log_warning "MCP SDK не найден в виртуальном окружении. Устанавливаем..."
            python3 -m pip install mcp || {
                log_error "Не удалось установить MCP SDK"
                exit 1
            }
        fi
    fi

    # Проверка наличия MCP серверов
    if [ ! -f "${DAP_SDK_ROOT}/docs/scripts/mcp_server.py" ]; then
        log_error "DAP SDK MCP сервер не найден: ${DAP_SDK_ROOT}/docs/scripts/mcp_server.py"
        exit 1
    fi

    if [ ! -f "${CELLFRAME_SDK_ROOT}/docs/scripts/mcp_server.py" ]; then
        log_error "CellFrame SDK MCP сервер не найден: ${CELLFRAME_SDK_ROOT}/docs/scripts/mcp_server.py"
        exit 1
    fi

    log_success "Зависимости проверены"
}

# Функция для запуска MCP сервера
start_mcp_server() {
    local server_name="$1"
    local server_script="$2"
    local log_file="$3"

    log "Запуск $server_name MCP сервера..."

    # Проверка, не запущен ли уже сервер
    local pid_file="$PID_DIR/${server_name}_mcp.pid"
    if [ -f "$pid_file" ]; then
        local pid=$(cat "$pid_file")
        if kill -0 "$pid" 2>/dev/null; then
            log_warning "$server_name MCP сервер уже запущен (PID: $pid)"
            return 0
        else
            log "Удаление устаревшего PID файла: $pid_file"
            rm -f "$pid_file"
        fi
    fi

    # Запуск MCP сервера в фоне с использованием виртуального окружения
    nohup "${PROJECT_ROOT}/mcp_env/bin/python3" "$server_script" > "$log_file" 2>&1 &
    local server_pid=$!

    echo "$server_pid" > "$pid_file"

    # Ожидание запуска
    sleep 3

    # Проверка, что сервер запустился
    if kill -0 "$server_pid" 2>/dev/null; then
        log_success "$server_name MCP сервер запущен (PID: $server_pid)"
        log "Логи: $log_file"
        return 0
    else
        log_error "Не удалось запустить $server_name MCP сервер"
        log "Проверьте логи: $log_file"
        return 1
    fi
}

# Функция для остановки MCP сервера
stop_mcp_server() {
    local server_name="$1"
    local pid_file="$PID_DIR/${server_name}_mcp.pid"

    if [ -f "$pid_file" ]; then
        local pid=$(cat "$pid_file")
        if kill -0 "$pid" 2>/dev/null; then
            log "Остановка $server_name MCP сервера (PID: $pid)..."
            kill -TERM "$pid"

            # Ожидание завершения
            local count=0
            while kill -0 "$pid" 2>/dev/null && [ $count -lt 10 ]; do
                sleep 1
                count=$((count + 1))
            done

            if kill -0 "$pid" 2>/dev/null; then
                log_warning "Принудительная остановка $server_name MCP сервера..."
                kill -KILL "$pid"
            fi

            rm -f "$pid_file"
            log_success "$server_name MCP сервер остановлен"
        else
            log_warning "$server_name MCP сервер не запущен"
            rm -f "$pid_file"
        fi
    else
        log_warning "PID файл не найден для $server_name MCP сервера"
    fi
}

# Функция для проверки статуса MCP сервера
check_mcp_server_status() {
    local server_name="$1"
    local pid_file="$PID_DIR/${server_name}_mcp.pid"

    if [ -f "$pid_file" ]; then
        local pid=$(cat "$pid_file")
        if kill -0 "$pid" 2>/dev/null; then
            log_success "$server_name MCP сервер работает (PID: $pid)"
            return 0
        else
            log_error "$server_name MCP сервер не запущен (устаревший PID файл)"
            rm -f "$pid_file"
            return 1
        fi
    else
        log_error "$server_name MCP сервер не запущен"
        return 1
    fi
}

# Функция для отображения статуса всех MCP серверов
show_status() {
    log "Статус MCP серверов:"
    echo

    check_mcp_server_status "dap_sdk"
    check_mcp_server_status "cellframe_sdk"
}

# Функция для отображения логов
show_logs() {
    local server_name="$1"
    local log_file="$LOG_DIR/${server_name}_mcp.log"

    if [ -f "$log_file" ]; then
        log "Логи $server_name MCP сервера:"
        tail -n 50 "$log_file"
    else
        log_error "Лог файл не найден: $log_file"
    fi
}

# Функция для отображения справки
show_help() {
    echo "Использование: $0 [КОМАНДА] [СЕРВЕР]"
    echo
    echo "Команды:"
    echo "  start [сервер]    - Запуск MCP сервера(ов)"
    echo "  stop [сервер]     - Остановка MCP сервера(ов)"
    echo "  restart [сервер]  - Перезапуск MCP сервера(ов)"
    echo "  status            - Показать статус всех MCP серверов"
    echo "  logs [сервер]     - Показать логи MCP сервера"
    echo "  help              - Показать эту справку"
    echo
    echo "Серверы:"
    echo "  dap_sdk           - DAP SDK MCP сервер"
    echo "  cellframe_sdk     - CellFrame SDK MCP сервер"
    echo "  all               - Все MCP серверы"
    echo
    echo "Примеры:"
    echo "  $0 start all                    # Запустить все MCP серверы"
    echo "  $0 start dap_sdk                # Запустить только DAP SDK MCP сервер"
    echo "  $0 stop cellframe_sdk           # Остановить CellFrame SDK MCP сервер"
    echo "  $0 status                       # Показать статус всех MCP серверов"
    echo "  $0 logs dap_sdk                 # Показать логи DAP SDK MCP сервера"
    echo
    echo "MCP серверы предоставляют инструменты для анализа кода через Model Context Protocol"
    echo "и интеграции с AI-системами (Claude, ChatGPT и др.)"
}

# Основная функция
main() {
    local command="$1"
    local server="$2"

    case "$command" in
        "start")
            check_dependencies

            case "$server" in
                "dap_sdk")
                    start_mcp_server "dap_sdk" "${DAP_SDK_ROOT}/docs/scripts/mcp_server.py" "${LOG_DIR}/dap_sdk_mcp.log"
                    ;;
                "cellframe_sdk")
                    start_mcp_server "cellframe_sdk" "${CELLFRAME_SDK_ROOT}/docs/scripts/mcp_server.py" "${LOG_DIR}/cellframe_sdk_mcp.log"
                    ;;
                "all"|"")
                    start_mcp_server "dap_sdk" "${DAP_SDK_ROOT}/docs/scripts/mcp_server.py" "${LOG_DIR}/dap_sdk_mcp.log"
                    start_mcp_server "cellframe_sdk" "${CELLFRAME_SDK_ROOT}/docs/scripts/mcp_server.py" "${LOG_DIR}/cellframe_sdk_mcp.log"
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
                "dap_sdk")
                    stop_mcp_server "dap_sdk"
                    ;;
                "cellframe_sdk")
                    stop_mcp_server "cellframe_sdk"
                    ;;
                "all"|"")
                    stop_mcp_server "dap_sdk"
                    stop_mcp_server "cellframe_sdk"
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
                "dap_sdk")
                    stop_mcp_server "dap_sdk"
                    sleep 2
                    start_mcp_server "dap_sdk" "${DAP_SDK_ROOT}/docs/scripts/mcp_server.py" "${LOG_DIR}/dap_sdk_mcp.log"
                    ;;
                "cellframe_sdk")
                    stop_mcp_server "cellframe_sdk"
                    sleep 2
                    start_mcp_server "cellframe_sdk" "${CELLFRAME_SDK_ROOT}/docs/scripts/mcp_server.py" "${LOG_DIR}/cellframe_sdk_mcp.log"
                    ;;
                "all"|"")
                    stop_mcp_server "dap_sdk"
                    stop_mcp_server "cellframe_sdk"
                    sleep 2
                    start_mcp_server "dap_sdk" "${DAP_SDK_ROOT}/docs/scripts/mcp_server.py" "${LOG_DIR}/dap_sdk_mcp.log"
                    start_mcp_server "cellframe_sdk" "${CELLFRAME_SDK_ROOT}/docs/scripts/mcp_server.py" "${LOG_DIR}/cellframe_sdk_mcp.log"
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
trap 'log "Получен сигнал завершения, остановка MCP серверов..."; exit 0' INT TERM

# Запуск основной функции
main "$@"
