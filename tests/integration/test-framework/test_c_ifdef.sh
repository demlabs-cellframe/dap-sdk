#!/bin/bash
# Integration test for c_ifdef mock extension

set -e

TEST_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DAP_SDK_DIR="$(cd "$TEST_DIR/../../.." && pwd)"
TEST_FRAMEWORK_DIR="${DAP_SDK_DIR}/test-framework"
MOCKS_DIR="${TEST_FRAMEWORK_DIR}/mocks"
DAP_TPL_DIR="${TEST_FRAMEWORK_DIR}/dap_tpl"
TEMPLATES_DIR="${DAP_SDK_DIR}/tests/fixtures/test-framework"

export SCRIPTS_DIR="${DAP_TPL_DIR}"
export TEMPLATES_DIR
export AWKPATH="${DAP_TPL_DIR}"

mkdir -p "${TEMPLATES_DIR}"

# Build engine with mocking extensions and capture path
MOCKING_DIR="${MOCKS_DIR}/lib/dap_tpl"
ENGINE_OUTPUT=$("${DAP_TPL_DIR}/utils/build_engine.sh" "${MOCKING_DIR}" 2>&1)
COMPILED_ENGINE=$(echo "$ENGINE_OUTPUT" | grep "^ENGINE_PATH=" | cut -d'=' -f2-)
if [ -z "$COMPILED_ENGINE" ] || [ ! -f "$COMPILED_ENGINE" ]; then
    echo "ERROR: Failed to build engine or find engine file"
    echo "$ENGINE_OUTPUT"
    exit 1
fi

# Test c_ifdef with true condition
echo "Test: c_ifdef with true condition"
TEST_TEMPLATE="${TEMPLATES_DIR}/test_c_ifdef.tpl"
cat > "$TEST_TEMPLATE" << 'EOF'
{{#c_ifdef FEATURE_ENABLED}}
int feature_function() {
    return 1;
}
{{/c_ifdef}}
EOF

export FEATURE_ENABLED="1"
# Use temp file to preserve trailing newline
TEMP_OUTPUT=$(mktemp)
cd "$DAP_TPL_DIR" && gawk -f "$COMPILED_ENGINE" "$TEST_TEMPLATE" > "$TEMP_OUTPUT" 2>&1
# Read file - command substitution removes trailing newline
OUTPUT=$(cat "$TEMP_OUTPUT")
# File ends with \n\n (one from #endif\n, one from print)
# Command substitution $() removes trailing newline, so we need to add it back
# But EXPECTED has only one \n, so we need to remove one \n first
# Remove the extra newline added by print, then add one back to match EXPECTED
OUTPUT="${OUTPUT%$'\n'}"  # Remove trailing newline from print
OUTPUT="${OUTPUT}"$'\n'   # Add one newline to match EXPECTED format
rm -f "$TEMP_OUTPUT"
EXPECTED="#ifdef FEATURE_ENABLED
int feature_function() {
    return 1;
}
#endif
"

if [ "$OUTPUT" != "$EXPECTED" ]; then
    echo "FAIL: c_ifdef with true condition"
    echo "Expected:"
    echo "$EXPECTED" | cat -A
    echo "Got:"
    echo "$OUTPUT" | cat -A
    exit 1
fi
echo "PASS: c_ifdef with true condition"

# Test c_ifdef with false condition
echo "Test: c_ifdef with false condition"
export FEATURE_ENABLED="0"
TEMP_OUTPUT=$(mktemp)
cd "$DAP_TPL_DIR" && gawk -f "$COMPILED_ENGINE" "$TEST_TEMPLATE" > "$TEMP_OUTPUT" 2>&1
OUTPUT=$(cat "$TEMP_OUTPUT")
OUTPUT="${OUTPUT%$'\n'}"  # Remove trailing newline from print
OUTPUT="${OUTPUT}"$'\n'   # Add one newline to match EXPECTED format
rm -f "$TEMP_OUTPUT"
EXPECTED="#ifdef FEATURE_ENABLED
#endif
"

if [ "$OUTPUT" != "$EXPECTED" ]; then
    echo "FAIL: c_ifdef with false condition"
    echo "Expected:"
    echo "$EXPECTED" | cat -A
    echo "Got:"
    echo "$OUTPUT" | cat -A
    exit 1
fi
echo "PASS: c_ifdef with false condition"

# Test c_ifdef with elif and else
echo "Test: c_ifdef with elif and else"
TEST_TEMPLATE2="${TEMPLATES_DIR}/test_c_ifdef_elif.tpl"
cat > "$TEST_TEMPLATE2" << 'EOF'
{{#c_ifdef FEATURE_A}}
Feature A code
{{#c_elif FEATURE_B}}
Feature B code
{{#c_else}}
Default code
{{/c_ifdef}}
EOF

export FEATURE_A="0"
export FEATURE_B="1"
TEMP_OUTPUT2=$(mktemp)
cd "$DAP_TPL_DIR" && gawk -f "$COMPILED_ENGINE" "$TEST_TEMPLATE2" > "$TEMP_OUTPUT2" 2>&1
OUTPUT2=$(cat "$TEMP_OUTPUT2")
OUTPUT2="${OUTPUT2%$'\n'}"  # Remove trailing newline from print
OUTPUT2="${OUTPUT2}"$'\n'   # Add one newline to match EXPECTED format
rm -f "$TEMP_OUTPUT2"
EXPECTED2="#ifdef FEATURE_A
#elifdef FEATURE_B
Feature B code
#endif
"

if [ "$OUTPUT2" != "$EXPECTED2" ]; then
    echo "FAIL: c_ifdef with elif"
    echo "Expected:"
    echo "$EXPECTED2" | cat -A
    echo "Got:"
    echo "$OUTPUT2" | cat -A
    exit 1
fi
echo "PASS: c_ifdef with elif"

echo "All c_ifdef tests passed!"

