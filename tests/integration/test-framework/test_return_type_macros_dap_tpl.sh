#!/bin/bash
# Mock framework test: Test return_type_macros.h.tpl using pure dap_tpl constructs
# This test verifies that the mock template works correctly with dap_tpl features

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DAP_SDK_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"
TEST_FRAMEWORK_DIR="${DAP_SDK_DIR}/test-framework"
MOCKS_DIR="${TEST_FRAMEWORK_DIR}/mocks"
DAP_TPL_DIR="${TEST_FRAMEWORK_DIR}/dap_tpl"
TEMPLATES_DIR="${TEST_FRAMEWORK_DIR}/templates"

TEST_TEMPLATE="${TEMPLATES_DIR}/return_type_macros.h.tpl"
TEST_OUTPUT="/tmp/test_return_type_macros_output.h"

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

# Test 1: Simple RETURN_TYPES with void and int
echo "=========================================="
echo "TEST 1: RETURN_TYPES=void int"
echo "=========================================="
export RETURN_TYPES="void int"
export ORIGINAL_TYPES_DATA=""
export BASIC_TYPES_RAW_DATA=""

gawk -f "$ENGINE_PATH" "$TEST_TEMPLATE" > "${TEST_OUTPUT}" 2>&1

# Check that void macro is generated
if ! grep -q "_DAP_MOCK_WRAPPER_CUSTOM_SELECT_void" "${TEST_OUTPUT}"; then
    echo "FAIL: void macro not generated"
    cat "${TEST_OUTPUT}"
    exit 1
fi

# Check that int macro is generated
if ! grep -q "_DAP_MOCK_WRAPPER_CUSTOM_SELECT_int" "${TEST_OUTPUT}"; then
    echo "FAIL: int macro not generated"
    cat "${TEST_OUTPUT}"
    exit 1
fi

# Check that void macro calls VOID version
if ! grep -q "_DAP_MOCK_WRAPPER_CUSTOM_VOID" "${TEST_OUTPUT}"; then
    echo "FAIL: void macro does not call VOID version"
    cat "${TEST_OUTPUT}"
    exit 1
fi

# Check that int macro calls NONVOID version
if ! grep -q "_DAP_MOCK_WRAPPER_CUSTOM_NONVOID" "${TEST_OUTPUT}"; then
    echo "FAIL: int macro does not call NONVOID version"
    cat "${TEST_OUTPUT}"
    exit 1
fi

echo "PASS: Basic RETURN_TYPES processing works"
echo ""

# Test 2: With ORIGINAL_TYPES_DATA
echo "=========================================="
echo "TEST 2: RETURN_TYPES with ORIGINAL_TYPES_DATA"
echo "=========================================="
export RETURN_TYPES="int char"
export ORIGINAL_TYPES_DATA="int|int
char|char"
export BASIC_TYPES_RAW_DATA=""

gawk -f "$ENGINE_PATH" "$TEST_TEMPLATE" > "${TEST_OUTPUT}" 2>&1

# Check that normalization macros are generated
if ! grep -q "_DAP_MOCK_NORMALIZE_TYPE_int" "${TEST_OUTPUT}"; then
    echo "FAIL: Normalization macro for int not generated"
    cat "${TEST_OUTPUT}"
    exit 1
fi

if ! grep -q "_DAP_MOCK_NORMALIZE_TYPE_char" "${TEST_OUTPUT}"; then
    echo "FAIL: Normalization macro for char not generated"
    cat "${TEST_OUTPUT}"
    exit 1
fi

echo "PASS: ORIGINAL_TYPES_DATA processing works"
echo ""

# Test 3: With BASIC_TYPES_RAW_DATA
echo "=========================================="
echo "TEST 3: RETURN_TYPES with BASIC_TYPES_RAW_DATA"
echo "=========================================="
export RETURN_TYPES="void"
export ORIGINAL_TYPES_DATA=""
export BASIC_TYPES_RAW_DATA="int|int
char|char"

gawk -f "$ENGINE_PATH" "$TEST_TEMPLATE" > "${TEST_OUTPUT}" 2>&1

# Check that basic type macros are generated
if ! grep -q "_DAP_MOCK_NORMALIZE_TYPE_int" "${TEST_OUTPUT}"; then
    echo "FAIL: Basic type macro for int not generated"
    cat "${TEST_OUTPUT}"
    exit 1
fi

if ! grep -q "_DAP_MOCK_TYPE_TO_SELECT_NAME_int" "${TEST_OUTPUT}"; then
    echo "FAIL: Type-to-selector macro for int not generated"
    cat "${TEST_OUTPUT}"
    exit 1
fi

echo "PASS: BASIC_TYPES_RAW_DATA processing works"
echo ""

# Test 4: Without RETURN_TYPES (should skip entire block)
echo "=========================================="
echo "TEST 4: RETURN_TYPES unset (should skip)"
echo "=========================================="
unset RETURN_TYPES
unset ORIGINAL_TYPES_DATA
unset BASIC_TYPES_RAW_DATA

gawk -f "$ENGINE_PATH" "$TEST_TEMPLATE" > "${TEST_OUTPUT}" 2>&1

# Check that no macros are generated
if grep -q "#define _DAP_MOCK" "${TEST_OUTPUT}"; then
    echo "FAIL: Macros generated when RETURN_TYPES is unset"
    cat "${TEST_OUTPUT}"
    exit 1
fi

echo "PASS: Template correctly skips when RETURN_TYPES unset"
echo ""

rm -f "${TEST_OUTPUT}"
echo "=========================================="
echo "ALL TESTS PASSED"
echo "=========================================="

