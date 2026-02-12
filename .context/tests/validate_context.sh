#!/bin/bash

# Скрипт для валидации контекстных файлов DAP SDK
# Проверяет структуру, JSON валидность и консистентность данных

set -e

CONTEXT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT_ROOT="$(cd "$CONTEXT_DIR/.." && pwd)"

# Цвета для вывода
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Счетчики
TESTS_TOTAL=0
TESTS_PASSED=0
TESTS_FAILED=0

# Функция для вывода результата теста
test_result() {
    local test_name="$1"
    local result="$2"
    local message="$3"
    
    TESTS_TOTAL=$((TESTS_TOTAL + 1))
    
    if [[ "$result" == "PASS" ]]; then
        echo -e "${GREEN}✅ PASS${NC}: $test_name"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        echo -e "${RED}❌ FAIL${NC}: $test_name"
        if [[ -n "$message" ]]; then
            echo -e "   ${YELLOW}↳${NC} $message"
        fi
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
}

# Проверка JSON валидности
validate_json() {
    local file="$1"
    local test_name="JSON валидность: $(basename "$file")"
    
    if [[ ! -f "$file" ]]; then
        test_result "$test_name" "FAIL" "Файл не найден"
        return 1
    fi
    
    if command -v jq >/dev/null 2>&1; then
        if jq . "$file" >/dev/null 2>&1; then
            test_result "$test_name" "PASS"
            return 0
        else
            test_result "$test_name" "FAIL" "Некорректный JSON"
            return 1
        fi
    else
        # Простая проверка без jq
        if python3 -m json.tool "$file" >/dev/null 2>&1; then
            test_result "$test_name" "PASS"
            return 0
        else
            test_result "$test_name" "FAIL" "Некорректный JSON (проверено через Python)"
            return 1
        fi
    fi
}

# Проверка существования файла
check_file_exists() {
    local file="$1"
    local test_name="Существование файла: $(basename "$file")"
    
    if [[ -f "$file" ]]; then
        test_result "$test_name" "PASS"
        return 0
    else
        test_result "$test_name" "FAIL" "Файл не найден"
        return 1
    fi
}

# Проверка структуры папок
check_directory_structure() {
    local test_name="Структура папок"
    local required_dirs=(
        "$CONTEXT_DIR"
        "$CONTEXT_DIR/scripts"
        "$CONTEXT_DIR/tests" 
        "$CONTEXT_DIR/.local"
        "$CONTEXT_DIR/.local/scripts"
        "$CONTEXT_DIR/.local/tests"
    )
    
    local missing_dirs=()
    for dir in "${required_dirs[@]}"; do
        if [[ ! -d "$dir" ]]; then
            missing_dirs+=("$(basename "$dir")")
        fi
    done
    
    if [[ ${#missing_dirs[@]} -eq 0 ]]; then
        test_result "$test_name" "PASS"
        return 0
    else
        test_result "$test_name" "FAIL" "Отсутствуют папки: ${missing_dirs[*]}"
        return 1
    fi
}

# Проверка gitignore
check_gitignore() {
    local test_name="Настройка .gitignore"
    local gitignore_file="$PROJECT_ROOT/.gitignore"
    
    if [[ ! -f "$gitignore_file" ]]; then
        test_result "$test_name" "FAIL" ".gitignore не найден"
        return 1
    fi
    
    if grep -q "context/.local/" "$gitignore_file"; then
        test_result "$test_name" "PASS"
        return 0
    else
        test_result "$test_name" "FAIL" "context/.local/ не добавлен в .gitignore"
        return 1
    fi
}

# Проверка консистентности данных
check_data_consistency() {
    local test_name="Консистентность данных"
    local index_file="$CONTEXT_DIR/index.json"
    local local_context="$CONTEXT_DIR/.local/context.json"
    
    if [[ ! -f "$index_file" ]] || [[ ! -f "$local_context" ]]; then
        test_result "$test_name" "FAIL" "Отсутствуют файлы для проверки"
        return 1
    fi
    
    # Проверяем, что task_id в локальном контексте соответствует индексу
    if command -v jq >/dev/null 2>&1; then
        local task_id_local=$(jq -r '.task.id' "$local_context" 2>/dev/null)
        local task_exists=$(jq -r ".tasks | has(\"$task_id_local\")" "$index_file" 2>/dev/null)
        
        if [[ "$task_exists" == "true" ]]; then
            test_result "$test_name" "PASS"
            return 0
        else
            test_result "$test_name" "FAIL" "task_id $task_id_local не найден в индексе"
            return 1
        fi
    else
        test_result "$test_name" "PASS" "Пропущен (нет jq)"
        return 0
    fi
}

# Основная функция
main() {
    echo -e "${BLUE}🧪 DAP SDK Context Validation${NC}"
echo -e "${BLUE}📁 Context: $CONTEXT_DIR${NC}"
    echo ""
    
    # Проверка структуры папок
    check_directory_structure
    
    # Проверка основных файлов
    check_file_exists "$CONTEXT_DIR/index.json"
    check_file_exists "$CONTEXT_DIR/structure.json"
    check_file_exists "$CONTEXT_DIR/context.json"
    
    # Проверка локальных файлов
    check_file_exists "$CONTEXT_DIR/.local/context.json"
    check_file_exists "$CONTEXT_DIR/.local/progress.json"
    check_file_exists "$CONTEXT_DIR/.local/index.json"
    
    # JSON валидация
    validate_json "$CONTEXT_DIR/index.json"
    validate_json "$CONTEXT_DIR/structure.json"
    validate_json "$CONTEXT_DIR/context.json"
    validate_json "$CONTEXT_DIR/.local/context.json"
    validate_json "$CONTEXT_DIR/.local/progress.json"
    validate_json "$CONTEXT_DIR/.local/index.json"
    
    # Проверка настроек
    check_gitignore
    check_data_consistency
    
    # Итоговая статистика
    echo ""
    echo -e "${BLUE}📊 Validation Results:${NC}"
    echo -e "   Total tests: $TESTS_TOTAL"
    echo -e "   ${GREEN}Passed: $TESTS_PASSED${NC}"
    echo -e "   ${RED}Failed: $TESTS_FAILED${NC}"
    
    if [[ $TESTS_FAILED -eq 0 ]]; then
        echo ""
        echo -e "${GREEN}🎉 All tests passed successfully!${NC}"
        echo -e "${GREEN}✅ Context is configured correctly${NC}"
        exit 0
    else
        echo ""
        echo -e "${RED}❌ Configuration issues detected${NC}"
        exit 1
    fi
}

# Запуск
main "$@" 