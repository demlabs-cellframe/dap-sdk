#!/bin/bash
# Test runner for mocking-specific variable functions tests
# Tests functions from mocks/lib/dap_tpl/variable_functions.awk

set -e

# Initialize paths
TEST_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DAP_SDK_DIR="$(cd "$TEST_DIR/../../.." && pwd)"
TEST_FRAMEWORK_DIR="${DAP_SDK_DIR}/test-framework"
MOCKS_DIR="${TEST_FRAMEWORK_DIR}/mocks"
DAP_TPL_DIR="${TEST_FRAMEWORK_DIR}/dap_tpl"

# Set AWKPATH and SCRIPTS_DIR directly
# AWKPATH must include: dap_tpl (for core modules), test-framework root (for mocks/lib/dap_tpl)
export AWKPATH="${DAP_TPL_DIR}:${TEST_FRAMEWORK_DIR}:${MOCKS_DIR}/lib/dap_tpl"
export SCRIPTS_DIR="${DAP_TPL_DIR}"

# Initialize counters
failed=0
passed=0

TESTS_DIR="${TEST_DIR}/variable_functions_tests"

echo "Running mocking variable_functions tests..."

# Run all tests in directory (need to run from DAP_TPL_DIR for @include to work)
for test_file in "$TESTS_DIR"/*.awk; do
    if [ ! -f "$test_file" ]; then
        continue
    fi
    
    test_name=$(basename "$test_file" .awk)
    echo -n "  $test_name: "
    
    if result=$(cd "$DAP_TPL_DIR" && AWKPATH="${AWKPATH}" gawk -f "$test_file" <<< "" 2>&1); then
        if echo "$result" | grep -q "^PASS$"; then
            echo "PASS"
            ((passed++)) || true
        else
            echo "FAIL"
            echo "    Output: $result"
            ((failed++)) || true
        fi
    else
        echo "FAIL"
        echo "    Error: $result"
        ((failed++)) || true
    fi
done

echo ""
if [ $failed -eq 0 ]; then
    echo "All $passed mocking variable_function tests passed!"
    exit 0
else
    echo "$failed mocking variable_function test(s) failed, $passed mocking variable_function test(s) passed"
    exit 1
fi
