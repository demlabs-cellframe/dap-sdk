#!/usr/bin/awk -f
# Scan source files for DAP_MOCK_DECLARE and DAP_MOCK_DECLARE_CUSTOM declarations
# Output: newline-separated list of function names
# MAWK COMPATIBLE - no capture groups (arr parameter)

{
    # Find DAP_MOCK_DECLARE(func_name) or DAP_MOCK_DECLARE(func_name, ...)
    while (match($0, /DAP_MOCK_DECLARE\s*\(\s*[a-zA-Z_][a-zA-Z0-9_]*/)) {
        matched = substr($0, RSTART, RLENGTH)
        # Extract function name: everything after opening paren
        if (match(matched, /\(\s*[a-zA-Z_][a-zA-Z0-9_]*/)) {
            func_part = substr(matched, RSTART, RLENGTH)
            # Remove opening paren and whitespace
            gsub(/^\(\s*/, "", func_part)
            if (func_part != "") {
                print func_part
            }
        }
        # Remove matched part and continue searching
        $0 = substr($0, RSTART + RLENGTH)
    }
    
    # Reset line for CUSTOM pattern
    $0 = $0
    
    # Find DAP_MOCK_DECLARE_CUSTOM(func_name, ...)
    while (match($0, /DAP_MOCK_DECLARE_CUSTOM\s*\(\s*[a-zA-Z_][a-zA-Z0-9_]*/)) {
        matched = substr($0, RSTART, RLENGTH)
        # Extract function name: everything after opening paren
        if (match(matched, /\(\s*[a-zA-Z_][a-zA-Z0-9_]*/)) {
            func_part = substr(matched, RSTART, RLENGTH)
            # Remove opening paren and whitespace
            gsub(/^\(\s*/, "", func_part)
            if (func_part != "") {
                print func_part
            }
        }
        # Remove matched part and continue searching
        $0 = substr($0, RSTART + RLENGTH)
    }
}

