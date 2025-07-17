#!/bin/bash

# Скрипт для обновления контекстных файлов DAP SDK
# Автоматически обновляет индексы и статистику

set -e

CONTEXT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT_ROOT="$(cd "$CONTEXT_DIR/.." && pwd)"

echo "🔄 Updating DAP SDK context..."
echo "📁 Context: $CONTEXT_DIR"
echo "📁 Project: $PROJECT_ROOT"

# Функция для обновления временных меток
update_timestamp() {
    local file="$1"
    if [[ -f "$file" ]]; then
        # Обновляем поле updated в JSON файле
        local timestamp=$(date -u +"%Y-%m-%dT%H:%M:%SZ")
        # Используем jq если доступен, иначе sed
        if command -v jq >/dev/null 2>&1; then
            jq --arg ts "$timestamp" '.updated = $ts' "$file" > "$file.tmp" && mv "$file.tmp" "$file"
        else
            sed -i.bak "s/\"updated\": \"[^\"]*\"/\"updated\": \"$timestamp\"/" "$file"
            rm -f "$file.bak"
        fi
        echo "✅ Updated: $(basename "$file")"
    fi
}

# Функция для подсчета статистики
calculate_stats() {
    echo "📊 Calculating statistics..."
    
    # Подсчет файлов в crypto/src/chipmunk
    local chipmunk_dir="$PROJECT_ROOT/crypto/src/chipmunk"
    if [[ -d "$chipmunk_dir" ]]; then
        local total_files=$(find "$chipmunk_dir" -name "*.c" -o -name "*.h" | wc -l)
        local total_lines=$(find "$chipmunk_dir" -name "*.c" -o -name "*.h" -exec wc -l {} + | tail -1 | awk '{print $1}')
        echo "📈 Chipmunk files: $total_files"
        echo "📈 Chipmunk lines: $total_lines"
    fi
}

# Основная логика
main() {
    echo ""
    echo "🔍 Checking structure..."
    
    # Проверяем существование основных файлов
    local files_to_check=(
        "$CONTEXT_DIR/index.json"
        "$CONTEXT_DIR/structure.json"
        "$CONTEXT_DIR/context.json"
    )
    
    for file in "${files_to_check[@]}"; do
        if [[ -f "$file" ]]; then
            echo "✅ Found: $(basename "$file")"
        else
            echo "❌ Missing: $(basename "$file")"
        fi
    done
    
    echo ""
    echo "🕐 Updating timestamps..."
    
    # Обновляем временные метки
    update_timestamp "$CONTEXT_DIR/index.json"
    update_timestamp "$CONTEXT_DIR/context.json"
    
    # Обновляем локальные файлы если они существуют
    if [[ -d "$CONTEXT_DIR/.local" ]]; then
        echo "📁 Updating local files..."
        update_timestamp "$CONTEXT_DIR/.local/context.json"
        update_timestamp "$CONTEXT_DIR/.local/progress.json"
        update_timestamp "$CONTEXT_DIR/.local/index.json"
    fi
    
    echo ""
    calculate_stats
    
    echo ""
    echo "✅ Context updated successfully!"
    echo "📝 Files ready for commit (except .local/)"
}

# Запуск
main "$@" 