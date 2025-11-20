#!/bin/bash
# Mock framework test: Test dispatcher_macros.h.tpl using pure dap_tpl constructs
# This test verifies that the mock template works correctly with dap_tpl features

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DAP_SDK_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"
TEST_FRAMEWORK_DIR="${DAP_SDK_DIR}/test-framework"
MOCKS_DIR="${TEST_FRAMEWORK_DIR}/mocks"
DAP_TPL_DIR="${TEST_FRAMEWORK_DIR}/dap_tpl"
TEMPLATES_DIR="${TEST_FRAMEWORK_DIR}/templates"

TEST_TEMPLATE="${TEMPLATES_DIR}/dispatcher_macros.h.tpl"
TEST_OUTPUT="/tmp/test_dispatcher_macros_output.h"

# Load dap_tpl functions
cd "${DAP_TPL_DIR}"
source dap_tpl.sh

# Build engine with extensions
EXTENSIONS_DIR="${MOCKS_DIR}/lib/dap_tpl"
ENGINE_OUTPUT=$(./utils/build_engine.sh "${EXTENSIONS_DIR}" 2>&1)
ENGINE_PATH=$(echo "$ENGINE_OUTPUT" | grep "^ENGINE_PATH=" | cut -d'=' -f2)

if [ -z "$ENGINE_PATH" ] || [ ! -f "$ENGINE_PATH" ]; then
    echo "FAIL: Engine not built"
    exit 1
fi

echo "Using engine: $ENGINE_PATH"
echo ""

# Test 1: Simple ORIGINAL_TYPES_DATA with void and int
echo "=========================================="
echo "TEST 1: ORIGINAL_TYPES_DATA=void int"
echo "=========================================="
export ORIGINAL_TYPES_DATA="void|void
int|int"

gawk -f "$ENGINE_PATH" "$TEST_TEMPLATE" > "${TEST_OUTPUT}" 2>&1

# Check that void macro is generated
if ! grep -q "_DAP_MOCK_WRAPPER_CUSTOM_DISPATCH_void" "${TEST_OUTPUT}"; then
    echo "FAIL: void macro not generated"
    cat "${TEST_OUTPUT}"
    exit 1
fi

# Check that int macro is generated
if ! grep -q "_DAP_MOCK_WRAPPER_CUSTOM_DISPATCH_int" "${TEST_OUTPUT}"; then
    echo "FAIL: int macro not generated"
    cat "${TEST_OUTPUT}"
    exit 1
fi

# Check that void macro calls VOID_HELPER
if ! grep -q "_DAP_MOCK_WRAPPER_CUSTOM_DISPATCH_void_VOID_HELPER" "${TEST_OUTPUT}"; then
    echo "FAIL: void macro does not call VOID_HELPER"
    cat "${TEST_OUTPUT}"
    exit 1
fi

# Check that int macro calls NONVOID version
if ! grep -q "_DAP_MOCK_WRAPPER_CUSTOM_NONVOID" "${TEST_OUTPUT}"; then
    echo "FAIL: int macro does not call NONVOID version"
    cat "${TEST_OUTPUT}"
    exit 1
fi

echo "PASS: Basic ORIGINAL_TYPES_DATA processing works"
echo ""

# Test 2: Types with pointers (normalization)
echo "=========================================="
echo "TEST 2: Types with pointers (normalization)"
echo "=========================================="
export ORIGINAL_TYPES_DATA="dap_list_t_STAR|dap_list_t*
int|int"

gawk -f "$ENGINE_PATH" "$TEST_TEMPLATE" > "${TEST_OUTPUT}" 2>&1

# Check that normalized macro is generated
if ! grep -q "_DAP_MOCK_WRAPPER_CUSTOM_DISPATCH_dap_list_t_STAR" "${TEST_OUTPUT}"; then
    echo "FAIL: Normalized macro for dap_list_t_STAR not generated"
    cat "${TEST_OUTPUT}"
    exit 1
fi

echo "PASS: Normalization works"
echo ""

# Test 3: Without ORIGINAL_TYPES_DATA (should skip entire block)
echo "=========================================="
echo "TEST 3: ORIGINAL_TYPES_DATA unset (should skip)"
echo "=========================================="
unset ORIGINAL_TYPES_DATA

gawk -f "$ENGINE_PATH" "$TEST_TEMPLATE" > "${TEST_OUTPUT}" 2>&1

# Check that no macros are generated (file should be mostly empty comments)
if grep -q "#define _DAP_MOCK_WRAPPER_CUSTOM_DISPATCH" "${TEST_OUTPUT}"; then
    echo "FAIL: Macros generated when ORIGINAL_TYPES_DATA is unset"
    cat "${TEST_OUTPUT}"
    exit 1
fi

echo "PASS: Template correctly skips when ORIGINAL_TYPES_DATA unset"
echo ""

rm -f "${TEST_OUTPUT}"
echo "=========================================="
echo "ALL TESTS PASSED"
echo "=========================================="
