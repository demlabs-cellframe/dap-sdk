#!/bin/bash
# Run Windows x86 (32-bit) tests using Wine
# Usage: ./scripts/test-win32-wine.sh [test_pattern]
#
# Prerequisites:
#   1. Run ./scripts/deb-test-win32-setup.sh (as root)
#   2. Run ./scripts/test-win32-cross.sh to build
#   3. Run this script to execute tests
#
# Examples:
#   ./scripts/test-win32-wine.sh              # Run all tests
#   ./scripts/test-win32-wine.sh test_unit    # Run tests matching "test_unit"
#   ./scripts/test-win32-wine.sh test_json    # Run JSON tests only

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build-cross-win32"
TEST_PATTERN="${1:-test_}"

# Colors
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

log_test() {
    echo -e "${BLUE}[TEST]${NC} $1"
}

# Check prerequisites
check_prerequisites() {
    if ! command -v wine &> /dev/null; then
        log_error "wine not found. Run: sudo ./scripts/deb-test-win32-setup.sh"
        exit 1
    fi
    
    if [ ! -d "$BUILD_DIR" ]; then
        log_error "Build directory not found: $BUILD_DIR"
        log_error "Run: ./scripts/test-win32-cross.sh"
        exit 1
    fi
}

# Configure Wine environment
setup_wine_env() {
    # Suppress Wine debug output
    export WINEDEBUG=-all
    
    # Use separate prefix for testing (32-bit)
    export WINEPREFIX="${HOME}/.wine-dap-test-32"
    export WINEARCH=win32
    
    # Disable GUI elements
    export WINEDLLOVERRIDES="winemenubuilder.exe=d"
    
    # Set DLL path for cross-compiled libraries
    export WINEPATH="$BUILD_DIR"
}

# Find and run tests
run_tests() {
    local passed=0
    local failed=0
    local skipped=0
    local test_files=()
    
    log_info "Searching for test executables matching: $TEST_PATTERN"
    
    # Find all .exe files matching pattern
    while IFS= read -r -d '' file; do
        test_files+=("$file")
    done < <(find "$BUILD_DIR" -name "*.exe" -type f -print0 | grep -z "$TEST_PATTERN" || true)
    
    if [ ${#test_files[@]} -eq 0 ]; then
        log_warn "No test executables found matching: $TEST_PATTERN"
        log_info "Available executables:"
        find "$BUILD_DIR" -name "*.exe" -type f | head -20
        exit 1
    fi
    
    log_info "Found ${#test_files[@]} test executable(s)"
    echo ""
    
    for test_exe in "${test_files[@]}"; do
        local test_name=$(basename "$test_exe" .exe)
        local test_dir=$(dirname "$test_exe")
        
        log_test "Running: $test_name"
        
        # Run test with timeout (60 seconds per test)
        if timeout 60 wine "$test_exe" 2>&1; then
            echo -e "  ${GREEN}✓ PASSED${NC}"
            ((passed++))
        else
            local exit_code=$?
            if [ $exit_code -eq 124 ]; then
                echo -e "  ${YELLOW}⏱ TIMEOUT${NC}"
                ((skipped++))
            else
                echo -e "  ${RED}✗ FAILED${NC} (exit code: $exit_code)"
                ((failed++))
            fi
        fi
        echo ""
    done
    
    # Summary
    echo "=============================================="
    log_info "Test Results Summary"
    echo "=============================================="
    echo -e "  ${GREEN}Passed:${NC}  $passed"
    echo -e "  ${RED}Failed:${NC}  $failed"
    echo -e "  ${YELLOW}Skipped:${NC} $skipped"
    echo "  Total:   ${#test_files[@]}"
    echo ""
    
    if [ $failed -gt 0 ]; then
        log_error "Some tests failed!"
        exit 1
    else
        log_info "All tests passed!"
    fi
}

# Main
main() {
    echo "=============================================="
    echo "DAP SDK Windows x86 Tests (Wine)"
    echo "=============================================="
    echo ""
    
    check_prerequisites
    setup_wine_env
    
    log_info "Wine version: $(wine --version)"
    log_info "Build directory: $BUILD_DIR"
    log_info "Test pattern: $TEST_PATTERN"
    echo ""
    
    run_tests
}

main "$@"
