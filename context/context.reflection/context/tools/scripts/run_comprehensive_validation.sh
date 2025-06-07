#!/bin/bash

# 🧪 Comprehensive Template Validation Suite
# Master script для запуска всех validation tests

set -e  # Exit on any error

echo "🚀 Запуск Comprehensive Template Validation Suite"
echo "=================================================="

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
CONTEXT_ROOT="${1:-context/context.reflection/context}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOG_DIR="${SCRIPT_DIR}/../logs"
TIMESTAMP=$(date '+%Y%m%d_%H%M%S')

# Create logs directory
mkdir -p "$LOG_DIR"

echo -e "${BLUE}📁 Context Root: $CONTEXT_ROOT${NC}"
echo -e "${BLUE}📝 Logs Directory: $LOG_DIR${NC}"
echo ""

# Check if context root exists
if [ ! -d "$CONTEXT_ROOT" ]; then
    echo -e "${RED}❌ Error: Context directory '$CONTEXT_ROOT' not found${NC}"
    exit 1
fi

# Function to run test and capture results
run_test() {
    local test_name="$1"
    local test_command="$2"
    local log_file="$LOG_DIR/${test_name}_${TIMESTAMP}.log"
    
    echo -e "${BLUE}🔍 Running: $test_name${NC}"
    
    if eval "$test_command" > "$log_file" 2>&1; then
        echo -e "${GREEN}✅ $test_name: PASSED${NC}"
        return 0
    else
        echo -e "${RED}❌ $test_name: FAILED${NC}"
        echo -e "${YELLOW}   Log: $log_file${NC}"
        return 1
    fi
}

# Function to check Python availability
check_python() {
    if command -v python3 &> /dev/null; then
        echo "python3"
    elif command -v python &> /dev/null; then
        echo "python"
    else
        echo ""
    fi
}

# Check dependencies
echo -e "${BLUE}🔧 Checking dependencies...${NC}"

PYTHON_CMD=$(check_python)
if [ -z "$PYTHON_CMD" ]; then
    echo -e "${RED}❌ Python not found. Please install Python 3.x${NC}"
    exit 1
fi

echo -e "${GREEN}✅ Python found: $PYTHON_CMD${NC}"

# Check if jq is available for JSON validation
if command -v jq &> /dev/null; then
    JQ_AVAILABLE=true
    echo -e "${GREEN}✅ jq found for JSON validation${NC}"
else
    JQ_AVAILABLE=false
    echo -e "${YELLOW}⚠️  jq not found - JSON validation will be limited${NC}"
fi

echo ""

# Initialize counters
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0

# Test 1: JSON Structure Validation
echo -e "${BLUE}📋 Phase 1: JSON Structure Validation${NC}"
if [ "$JQ_AVAILABLE" = true ]; then
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    if run_test "json_structure_validation" "find '$CONTEXT_ROOT' -name '*.json' -exec jq . {} \; > /dev/null"; then
        PASSED_TESTS=$((PASSED_TESTS + 1))
    else
        FAILED_TESTS=$((FAILED_TESTS + 1))
    fi
else
    echo -e "${YELLOW}⚠️  Skipping JSON validation (jq not available)${NC}"
fi

# Test 2: Python Template Validation
echo -e "${BLUE}📋 Phase 2: Template Validation${NC}"
TOTAL_TESTS=$((TOTAL_TESTS + 1))
if run_test "template_validation" "$PYTHON_CMD '$SCRIPT_DIR/validate_all_templates.py' '$CONTEXT_ROOT'"; then
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    FAILED_TESTS=$((FAILED_TESTS + 1))
fi

# Test 3: Edge Case Testing
echo -e "${BLUE}🧪 Phase 3: Edge Case Testing${NC}"
TOTAL_TESTS=$((TOTAL_TESTS + 1))
if run_test "edge_case_testing" "$PYTHON_CMD '$SCRIPT_DIR/run_edge_case_tests.py' '$CONTEXT_ROOT'"; then
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    FAILED_TESTS=$((FAILED_TESTS + 1))
fi

# Test 4: File Structure Validation
echo -e "${BLUE}📁 Phase 4: File Structure Validation${NC}"
TOTAL_TESTS=$((TOTAL_TESTS + 1))

# Check for required directories and files
REQUIRED_DIRS=(
    "modules/methodologies"
    "tools"
    "docs"
)

REQUIRED_FILES=(
    "modules/methodologies/universal_optimization.json"
    "tools/workflow_recommendation_engine.json"
    "docs/матрица_взаимосвязи_шаблонов.md"
)

STRUCTURE_OK=true

for dir in "${REQUIRED_DIRS[@]}"; do
    if [ ! -d "$CONTEXT_ROOT/$dir" ]; then
        echo -e "${RED}❌ Missing required directory: $dir${NC}"
        STRUCTURE_OK=false
    fi
done

for file in "${REQUIRED_FILES[@]}"; do
    if [ ! -f "$CONTEXT_ROOT/$file" ]; then
        echo -e "${RED}❌ Missing required file: $file${NC}"
        STRUCTURE_OK=false
    fi
done

if [ "$STRUCTURE_OK" = true ]; then
    echo -e "${GREEN}✅ file_structure_validation: PASSED${NC}"
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    echo -e "${RED}❌ file_structure_validation: FAILED${NC}"
    FAILED_TESTS=$((FAILED_TESTS + 1))
fi

# Test 5: Markdown Link Validation
echo -e "${BLUE}📝 Phase 5: Markdown Link Validation${NC}"
TOTAL_TESTS=$((TOTAL_TESTS + 1))

# Simple markdown link check
MARKDOWN_OK=true
while IFS= read -r -d '' file; do
    # Extract markdown links [text](url)
    if grep -P '\[([^\]]+)\]\(([^)]+)\)' "$file" > /dev/null; then
        # Check for obviously broken links (very basic check)
        if grep -P '\[([^\]]+)\]\(\s*\)' "$file" > /dev/null; then
            echo -e "${RED}❌ Empty link found in: $file${NC}"
            MARKDOWN_OK=false
        fi
    fi
done < <(find "$CONTEXT_ROOT" -name "*.md" -print0)

if [ "$MARKDOWN_OK" = true ]; then
    echo -e "${GREEN}✅ markdown_link_validation: PASSED${NC}"
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    echo -e "${RED}❌ markdown_link_validation: FAILED${NC}"
    FAILED_TESTS=$((FAILED_TESTS + 1))
fi

# Test 6: Performance Benchmark
echo -e "${BLUE}⚡ Phase 6: Performance Benchmark${NC}"
TOTAL_TESTS=$((TOTAL_TESTS + 1))

START_TIME=$(date +%s.%N)

# Simulate template operations
FILE_COUNT=$(find "$CONTEXT_ROOT" -type f \( -name "*.json" -o -name "*.md" \) | wc -l)
JSON_COUNT=$(find "$CONTEXT_ROOT" -name "*.json" | wc -l)
MD_COUNT=$(find "$CONTEXT_ROOT" -name "*.md" | wc -l)

END_TIME=$(date +%s.%N)
EXECUTION_TIME=$(echo "$END_TIME - $START_TIME" | bc)

echo -e "${BLUE}📊 Performance Results:${NC}"
echo -e "   Files processed: $FILE_COUNT (JSON: $JSON_COUNT, MD: $MD_COUNT)"
echo -e "   Execution time: ${EXECUTION_TIME}s"

# Check if performance is acceptable (< 5 seconds for basic operations)
if (( $(echo "$EXECUTION_TIME < 5.0" | bc -l) )); then
    echo -e "${GREEN}✅ performance_benchmark: PASSED${NC}"
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    echo -e "${RED}❌ performance_benchmark: FAILED (too slow)${NC}"
    FAILED_TESTS=$((FAILED_TESTS + 1))
fi

# Generate comprehensive report
echo ""
echo "================================================================"
echo -e "${BLUE}📊 COMPREHENSIVE VALIDATION REPORT${NC}"
echo "================================================================"

echo -e "Общая статистика:"
echo -e "  Всего тестов: $TOTAL_TESTS"
echo -e "  ${GREEN}✅ Прошли: $PASSED_TESTS${NC}"
echo -e "  ${RED}❌ Провалены: $FAILED_TESTS${NC}"

if [ $TOTAL_TESTS -gt 0 ]; then
    SUCCESS_RATE=$(echo "scale=1; $PASSED_TESTS * 100 / $TOTAL_TESTS" | bc)
    echo -e "  📊 Успешность: ${SUCCESS_RATE}%"
fi

echo ""
echo -e "${BLUE}📁 Logs сохранены в: $LOG_DIR${NC}"

# List log files
if [ -d "$LOG_DIR" ]; then
    echo -e "${BLUE}📋 Доступные логи:${NC}"
    for log_file in "$LOG_DIR"/*_${TIMESTAMP}.log; do
        if [ -f "$log_file" ]; then
            echo -e "   $(basename "$log_file")"
        fi
    done
fi

echo ""

# Final recommendations
if [ $FAILED_TESTS -eq 0 ]; then
    echo -e "${GREEN}🎉 Все тесты прошли успешно! Система готова к использованию.${NC}"
    exit 0
else
    echo -e "${RED}⚠️  Найдены проблемы. Рекомендации:${NC}"
    echo -e "   1. Проверьте логи для детальной информации"
    echo -e "   2. Исправьте критические ошибки перед продакшеном"
    echo -e "   3. Запустите тесты повторно после исправлений"
    exit 1
fi 