#!/bin/bash
# Universal test runner for DAP SDK tests
# Usage:
#   ./run.sh all                    - Run all tests (C tests + test-framework tests)
#   ./run.sh unit                   - Run all unit tests
#   ./run.sh integration            - Run all integration tests
#   ./run.sh test-framework         - Run only test-framework tests (AWK/shell)
#   ./run.sh c-tests                - Run only C tests (via ctest)
#   ./run.sh test_name              - Run specific test

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DAP_SDK_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
TEST_FRAMEWORK_DIR="${DAP_SDK_DIR}/test-framework"
MOCKS_DIR="${TEST_FRAMEWORK_DIR}/mocks"
DAP_TPL_DIR="${TEST_FRAMEWORK_DIR}/dap_tpl"

export SCRIPTS_DIR="${DAP_TPL_DIR}"
export AWKPATH="${DAP_TPL_DIR}:${TEST_FRAMEWORK_DIR}:${MOCKS_DIR}/lib/dap_tpl"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

print_info() {
    echo -e "${BLUE}$1${NC}"
}

print_success() {
    echo -e "${GREEN}$1${NC}"
}

print_error() {
    echo -e "${RED}$1${NC}"
}

print_warning() {
    echo -e "${YELLOW}$1${NC}"
}

# Function to check if C tests are built
check_ctest_available() {
    if command -v ctest >/dev/null 2>&1; then
        # Check if build directory exists and has CMakeCache.txt
        if [ -f "${DAP_SDK_DIR}/build/CMakeCache.txt" ]; then
            return 0
        fi
    fi
    return 1
}

# Function to run C tests via ctest
run_ctest() {
    local test_type="${1:-all}"  # unit, integration, or all
    
    if ! command -v ctest >/dev/null 2>&1; then
        print_warning "ctest not found. C tests skipped."
        print_info "To install ctest, install CMake package"
        return 1
    fi
    
    # Use build directory in dap-sdk/build
    local build_dir="${DAP_SDK_DIR}/build"
    
    if [ ! -f "${build_dir}/CMakeCache.txt" ]; then
        print_warning "Build directory not found: ${build_dir}"
        print_info "To build C tests, run:"
        print_info "  cd ${DAP_SDK_DIR}"
        print_info "  mkdir -p build && cd build"
        print_info "  cmake .."
        print_info "  make"
        return 1
    fi
    
    print_info "Running C tests via ctest..."
    cd "$build_dir"
    
    if [ "$test_type" == "unit" ]; then
        ctest -R "test_" --output-on-failure
    elif [ "$test_type" == "integration" ]; then
        ctest -R "test_" --output-on-failure
    else
        ctest --output-on-failure
    fi
    
    return $?
}

# Function to run a single .awk test file
run_awk_test() {
    local test_file="$1"
    local test_name=$(basename "$test_file" .awk)
    
    # Run test with gawk from dap_tpl directory where modules are visible
    local result
    local test_path="$test_file"
    if [[ ! "$test_path" =~ ^/ ]]; then
        # Relative path - try to find in test-framework directories
        if [ -f "${SCRIPT_DIR}/unit/test-framework/$test_file" ]; then
            test_path="${SCRIPT_DIR}/unit/test-framework/$test_file"
        elif [ -f "${SCRIPT_DIR}/integration/test-framework/$test_file" ]; then
            test_path="${SCRIPT_DIR}/integration/test-framework/$test_file"
        else
            test_path="${SCRIPT_DIR}/$test_file"
        fi
    fi
    
    if result=$(cd "$DAP_TPL_DIR" && gawk -f "$test_path" <<< "" 2>&1); then
        if echo "$result" | grep -q "^PASS$"; then
            echo "PASS"
            return 0
        else
            echo "FAIL"
            echo "    Output: $result"
            return 1
        fi
    else
        echo "FAIL"
        echo "    Error: $result"
        return 1
    fi
}

# Function to run a test runner script
run_test_runner() {
    local runner_file="$1"
    local runner_name=$(basename "$runner_file" .sh)
    
    if [ ! -x "$runner_file" ]; then
        chmod +x "$runner_file"
    fi
    
    print_info "Running $runner_name..."
    local output
    if output=$(bash "$runner_file" 2>&1); then
        echo "$output"
        return 0
    else
        echo "$output"
        print_error "❌ Test failed: $runner_name"
        return 1
    fi
}

# Function to find and run all .awk test files in a directory
run_awk_tests_in_dir() {
    local test_dir="$1"
    local failed=0
    local passed=0
    
    if [ ! -d "$test_dir" ]; then
        print_error "Test directory not found: $test_dir"
        return 1
    fi
    
    for test_file in "$test_dir"/*.awk; do
        if [ ! -f "$test_file" ]; then
            continue
        fi
        
        test_name=$(basename "$test_file" .awk)
        echo -n "  $test_name: "
        
        if run_awk_test "$test_file"; then
            ((passed++)) || true
        else
            ((failed++)) || true
        fi
    done
    
    if [ $failed -eq 0 ] && [ $passed -gt 0 ]; then
        echo ""
        print_success "All $passed tests passed!"
        return 0
    elif [ $failed -gt 0 ]; then
        echo ""
        print_error "$failed test(s) failed, $passed test(s) passed"
        return 1
    else
        return 0
    fi
}

# Function to run all tests in a directory (both .sh runners and .awk files)
run_tests_in_dir() {
    local test_dir="$1"
    local failed=0
    local passed=0
    local found_any=0
    local failed_tests=()
    
    if [ ! -d "$test_dir" ]; then
        print_error "Test directory not found: $test_dir"
        return 1
    fi
    
    # Run all .sh test runners (recursively)
    # These runners handle their own _tests subdirectories
    while IFS= read -r runner_file; do
        if [ -f "$runner_file" ]; then
            found_any=1
            runner_name=$(basename "$runner_file" .sh)
            if run_test_runner "$runner_file"; then
                ((passed++)) || true
            else
                ((failed++)) || true
                failed_tests+=("$runner_name")
            fi
            echo ""
        fi
    done < <(find "$test_dir" -name "test_*.sh" -type f | sort)
    
    # Also find standalone _tests directories that don't have a runner
    # (for cases where tests are organized without a runner script)
    while IFS= read -r subdir; do
        # Check if there's a corresponding runner script
        subdir_parent=$(dirname "$subdir")
        subdir_name=$(basename "$subdir" _tests)
        runner_file="${subdir_parent}/test_${subdir_name}.sh"
        
        if [ ! -f "$runner_file" ]; then
            # No runner found, run tests directly
            found_any=1
            print_info "Running $(basename "$subdir") tests..."
            if run_awk_tests_in_dir "$subdir"; then
                ((passed++)) || true
            else
                ((failed++)) || true
            fi
            echo ""
        fi
    done < <(find "$test_dir" -type d -name "*_tests" | sort)
    
    if [ $found_any -eq 0 ]; then
        print_warning "No tests found in $test_dir"
        return 0
    fi
    
    if [ $failed -eq 0 ] && [ $passed -gt 0 ]; then
        return 0
    elif [ $failed -gt 0 ]; then
        # Print list of failed tests
        if [ ${#failed_tests[@]} -gt 0 ]; then
            print_error "Failed tests in $(basename "$test_dir"):"
            for test in "${failed_tests[@]}"; do
                echo "  - $test"
            done
        fi
        return 1
    else
        return 0
    fi
}

# Function to run test-framework tests (AWK/shell)
run_test_framework_tests() {
    local test_type="${1:-all}"  # unit, integration, or all
    local unit_result=0
    local integration_result=0
    
    print_info "Running test-framework tests..."
    echo ""
    
    if [ "$test_type" == "all" ] || [ "$test_type" == "unit" ]; then
        print_info "=== Test-Framework Unit Tests ==="
        if run_tests_in_dir "${SCRIPT_DIR}/unit/test-framework"; then
            unit_result=0
        else
            unit_result=1
        fi
        echo ""
    fi
    
    if [ "$test_type" == "all" ] || [ "$test_type" == "integration" ]; then
        print_info "=== Test-Framework Integration Tests ==="
        if run_tests_in_dir "${SCRIPT_DIR}/integration/test-framework"; then
            integration_result=0
        else
            integration_result=1
        fi
        echo ""
    fi
    
    if [ $unit_result -eq 0 ] && [ $integration_result -eq 0 ]; then
        return 0
    else
        return 1
    fi
}

# Main logic
TARGET="${1:-all}"

if [ "$TARGET" == "all" ]; then
    print_info "Running all DAP SDK tests..."
    echo ""
    
    ctest_result=0
    test_framework_result=0
    
    # Run C tests
    print_info "=== C Tests (via ctest) ==="
    if run_ctest "all"; then
        ctest_result=0
    else
        ctest_result=1
    fi
    echo ""
    
    # Run test-framework tests
    print_info "=== Test-Framework Tests (AWK/Shell) ==="
    if run_test_framework_tests "all"; then
        test_framework_result=0
    else
        test_framework_result=1
    fi
    
    echo ""
    if [ $ctest_result -eq 0 ] && [ $test_framework_result -eq 0 ]; then
        print_success "✅ All tests passed!"
        exit 0
    else
        echo ""
        print_error "❌ Some tests failed:"
        if [ $ctest_result -ne 0 ]; then
            echo "  - C tests: FAILED"
        fi
        if [ $test_framework_result -ne 0 ]; then
            echo "  - Test-framework tests: FAILED"
        fi
        exit 1
    fi
    
elif [ "$TARGET" == "unit" ]; then
    print_info "Running all unit tests..."
    echo ""
    
    ctest_result=0
    test_framework_result=0
    
    # Run C unit tests
    print_info "=== C Unit Tests ==="
    if run_ctest "unit"; then
        ctest_result=0
    else
        ctest_result=1
    fi
    echo ""
    
    # Run test-framework unit tests
    print_info "=== Test-Framework Unit Tests ==="
    if run_test_framework_tests "unit"; then
        test_framework_result=0
    else
        test_framework_result=1
    fi
    
    if [ $ctest_result -eq 0 ] && [ $test_framework_result -eq 0 ]; then
        exit 0
    else
        exit 1
    fi
    
elif [ "$TARGET" == "integration" ]; then
    print_info "Running all integration tests..."
    echo ""
    
    ctest_result=0
    test_framework_result=0
    
    # Run C integration tests
    print_info "=== C Integration Tests ==="
    if run_ctest "integration"; then
        ctest_result=0
    else
        ctest_result=1
    fi
    echo ""
    
    # Run test-framework integration tests
    print_info "=== Test-Framework Integration Tests ==="
    if run_test_framework_tests "integration"; then
        test_framework_result=0
    else
        test_framework_result=1
    fi
    
    if [ $ctest_result -eq 0 ] && [ $test_framework_result -eq 0 ]; then
        exit 0
    else
        exit 1
    fi
    
elif [ "$TARGET" == "test-framework" ]; then
    # Run only test-framework tests
    if run_test_framework_tests "all"; then
        exit 0
    else
        exit 1
    fi
    
elif [ "$TARGET" == "c-tests" ]; then
    # Run only C tests
    if run_ctest "all"; then
        exit 0
    else
        exit 1
    fi
    
elif [ -f "${SCRIPT_DIR}/unit/test-framework/${TARGET}.sh" ]; then
    # Specific test-framework test runner script
    run_test_runner "${SCRIPT_DIR}/unit/test-framework/${TARGET}.sh"
    exit $?
    
elif [ -f "${SCRIPT_DIR}/integration/test-framework/${TARGET}.sh" ]; then
    # Specific test-framework test runner script
    run_test_runner "${SCRIPT_DIR}/integration/test-framework/${TARGET}.sh"
    exit $?
    
elif [ -f "${SCRIPT_DIR}/${TARGET}.sh" ]; then
    # Specific test runner script
    run_test_runner "${SCRIPT_DIR}/${TARGET}.sh"
    exit $?
    
elif [ -f "${SCRIPT_DIR}/${TARGET}.awk" ]; then
    # Specific .awk test file
    print_info "Running test: $TARGET"
    if run_awk_test "${SCRIPT_DIR}/${TARGET}.awk"; then
        exit 0
    else
        exit 1
    fi
    
elif [ -d "${SCRIPT_DIR}/${TARGET}" ]; then
    # Directory - run all tests in it
    print_info "Running tests in: $TARGET"
    echo ""
    if run_tests_in_dir "${SCRIPT_DIR}/${TARGET}"; then
        exit 0
    else
        exit 1
    fi
    
else
    print_error "Unknown test target: $TARGET"
    echo ""
    echo "Usage: $0 [all|unit|integration|test-framework|c-tests|test_name|directory]"
    echo ""
    echo "Examples:"
    echo "  $0 all                          # Run all tests (C + test-framework)"
    echo "  $0 unit                         # Run all unit tests"
    echo "  $0 integration                  # Run all integration tests"
    echo "  $0 test-framework               # Run only test-framework tests (AWK/shell)"
    echo "  $0 c-tests                      # Run only C tests (via ctest)"
    echo "  $0 test_c_ifdef                 # Run specific test"
    echo "  $0 unit/test-framework          # Run tests in directory"
    exit 1
fi

