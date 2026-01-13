#!/usr/bin/gawk -f
# Scan source files for DAP_MOCK_DECLARE and DAP_MOCK_DECLARE_CUSTOM declarations
# Output: newline-separated list of function names

BEGIN {
    # Pattern for DAP_MOCK_DECLARE(func_name) or DAP_MOCK_DECLARE(func_name, ...)
    pattern_declare = "DAP_MOCK_DECLARE\\s*\\(\\s*([a-zA-Z_][a-zA-Z0-9_]*)"
    # Pattern for DAP_MOCK_DECLARE_CUSTOM(func_name, ...)
    pattern_declare_custom = "DAP_MOCK_DECLARE_CUSTOM\\s*\\(\\s*([a-zA-Z_][a-zA-Z0-9_]*)"
}

{
    # Find DAP_MOCK_DECLARE(func_name) or DAP_MOCK_DECLARE(func_name, ...)
    while (match($0, pattern_declare, arr)) {
        func_name = arr[1]
        if (func_name != "") {
            print func_name
        }
        # Remove matched part and continue searching
        $0 = substr($0, RSTART + RLENGTH)
    }
    
    # Reset line for next pattern
    $0 = $0
    
    # Find DAP_MOCK_DECLARE_CUSTOM(func_name, ...)
    while (match($0, pattern_declare_custom, arr)) {
        func_name = arr[1]
        if (func_name != "") {
            print func_name
        }
        # Remove matched part and continue searching
        $0 = substr($0, RSTART + RLENGTH)
    }
}

