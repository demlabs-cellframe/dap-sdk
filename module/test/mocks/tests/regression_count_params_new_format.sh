#!/bin/bash
# Regression test for count_params.awk
# Tests that it correctly counts parameters in both formats:
# 1. PARAM(type, name) - old format
# 2. (type name, type2 name2) - new format (parentheses without PARAM macro)

set -e

TEST_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MOCKS_DIR="$TEST_DIR/.."
AWK_SCRIPT="${MOCKS_DIR}/lib/awk/count_params.awk"

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

print_pass() { echo -e "${GREEN}PASS${NC}: $1"; }
print_fail() { echo -e "${RED}FAIL${NC}: $1"; exit 1; }

cleanup() {
    rm -f /tmp/test_count_params_*.c
}
trap cleanup EXIT

echo "=== Test count_params.awk with different parameter formats ==="
echo ""

# Test 1: Old format with PARAM macro
echo "Test 1: PARAM(type, name) format"
cat > /tmp/test_count_params_old.c << 'EOF'
DAP_MOCK_WRAPPER_CUSTOM(int, test_func,
    PARAM(int, a), PARAM(char*, b)
) {
}
EOF

OUTPUT1=$(gawk -f "$AWK_SCRIPT" /tmp/test_count_params_old.c)
COUNT1=$(echo "$OUTPUT1" | head -1)

if [ "$COUNT1" = "2" ]; then
    print_pass "Old format: 2 params detected correctly"
else
    print_fail "Old format: Expected 2, got: $COUNT1"
fi

# Test 2: New format with parentheses (NO PARAM macro)
echo "Test 2: (type name, type2 name2) format - NEW FORMAT"
cat > /tmp/test_count_params_new.c << 'EOF'
DAP_MOCK_WRAPPER_CUSTOM(int, test_func,
    (int a, char* b)
) {
}
EOF

OUTPUT2=$(gawk -f "$AWK_SCRIPT" /tmp/test_count_params_new.c)
COUNT2=$(echo "$OUTPUT2" | head -1)

if [ "$COUNT2" = "2" ]; then
    print_pass "New format: 2 params detected correctly"
else
    print_fail "New format: Expected 2, got: $COUNT2 - BUG FOUND! count_params.awk doesn't support new format!"
fi

# Test 3: Real-world example from test_ledger_tx_operations.c
echo "Test 3: Real example - 5 parameters"
cat > /tmp/test_count_params_real.c << 'EOF'
DAP_MOCK_WRAPPER_CUSTOM(int, dap_ledger_tx_add,
    (dap_ledger_t* a_ledger, dap_chain_datum_tx_t* a_tx, dap_hash_fast_t* a_tx_hash, bool a_from_threshold, dap_ledger_datum_iter_data_t* a_datum_index_data)
) {
}
EOF

OUTPUT3=$(gawk -f "$AWK_SCRIPT" /tmp/test_count_params_real.c)
COUNT3=$(echo "$OUTPUT3" | head -1)

if [ "$COUNT3" = "5" ]; then
    print_pass "Real example: 5 params detected correctly"
else
    print_fail "Real example: Expected 5, got: $COUNT3 - THIS IS THE BUG!"
fi

echo ""
echo "All tests passed!"
