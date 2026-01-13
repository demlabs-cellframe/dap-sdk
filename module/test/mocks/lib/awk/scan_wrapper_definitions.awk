#!/usr/bin/gawk -f
# Scan source files for wrapper definitions
# Output: newline-separated list of function names that have wrappers

BEGIN {
    # Pattern for DAP_MOCK_WRAPPER_CUSTOM(return_type, func_name, ...)
    # Extract func_name which is the second argument after the return type
    pattern_wrapper_custom = "DAP_MOCK_WRAPPER_CUSTOM\\s*\\([^,]*,\\s*([a-zA-Z_][a-zA-Z0-9_]*)"
    
    # Pattern for DAP_MOCK_WRAPPER_PASSTHROUGH(return_type, func_name, ...)
    pattern_wrapper_passthrough = "DAP_MOCK_WRAPPER_PASSTHROUGH\\s*\\([^,]*,\\s*([a-zA-Z_][a-zA-Z0-9_]*)"
    
    # Pattern for DAP_MOCK_WRAPPER_PASSTHROUGH_VOID(func_name, ...)
    pattern_wrapper_passthrough_void = "DAP_MOCK_WRAPPER_PASSTHROUGH_VOID\\s*\\(\\s*([a-zA-Z_][a-zA-Z0-9_]*)"
    
    # Pattern for explicit __wrap_ definitions
    pattern_wrap = "__wrap_([a-zA-Z_][a-zA-Z0-9_]*)"
}

{
    # Find DAP_MOCK_WRAPPER_CUSTOM(return_type, func_name, ...)
    while (match($0, pattern_wrapper_custom, arr)) {
        func_name = arr[1]
        if (func_name != "") {
            print func_name
        }
        # Remove matched part and continue searching
        $0 = substr($0, RSTART + RLENGTH)
    }
    
    # Reset line for next pattern
    $0 = $0
    
    # Find DAP_MOCK_WRAPPER_PASSTHROUGH(return_type, func_name, ...)
    while (match($0, pattern_wrapper_passthrough, arr)) {
        func_name = arr[1]
        if (func_name != "") {
            print func_name
        }
        # Remove matched part and continue searching
        $0 = substr($0, RSTART + RLENGTH)
    }
    
    # Reset line for next pattern
    $0 = $0
    
    # Find DAP_MOCK_WRAPPER_PASSTHROUGH_VOID(func_name, ...)
    while (match($0, pattern_wrapper_passthrough_void, arr)) {
        func_name = arr[1]
        if (func_name != "") {
            print func_name
        }
        # Remove matched part and continue searching
        $0 = substr($0, RSTART + RLENGTH)
    }
    
    # Reset line for next pattern
    $0 = $0
    
    # Find explicit __wrap_ definitions
    while (match($0, pattern_wrap, arr)) {
        func_name = arr[1]
        if (func_name != "") {
            print func_name
        }
        # Remove matched part and continue searching
        $0 = substr($0, RSTART + RLENGTH)
    }
}

