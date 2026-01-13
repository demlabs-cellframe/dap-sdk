#!/usr/bin/awk -f
# Scan source files for wrapper definitions
# Output: newline-separated list of function names that have wrappers
# MAWK COMPATIBLE - no capture groups

{
    # Find DAP_MOCK_WRAPPER_CUSTOM(return_type, func_name, ...)
    # func_name is the second argument after the first comma
    while (match($0, /DAP_MOCK_WRAPPER_CUSTOM\s*\([^,]*,\s*[a-zA-Z_][a-zA-Z0-9_]*/)) {
        matched = substr($0, RSTART, RLENGTH)
        # Extract everything after the first comma
        if (match(matched, /,\s*[a-zA-Z_][a-zA-Z0-9_]*/)) {
            func_part = substr(matched, RSTART, RLENGTH)
            # Remove comma and whitespace
            gsub(/^,\s*/, "", func_part)
            if (func_part != "") {
                print func_part
            }
        }
        $0 = substr($0, RSTART + RLENGTH)
    }
    
    $0 = $0
    
    # Find DAP_MOCK_WRAPPER_DEFAULT(return_type, func_name, ...)
    # func_name is the second argument after the first comma
    while (match($0, /DAP_MOCK_WRAPPER_DEFAULT\s*\([^,]*,\s*[a-zA-Z_][a-zA-Z0-9_]*/)) {
        matched = substr($0, RSTART, RLENGTH)
        # Extract everything after the first comma
        if (match(matched, /,\s*[a-zA-Z_][a-zA-Z0-9_]*/)) {
            func_part = substr(matched, RSTART, RLENGTH)
            # Remove comma and whitespace
            gsub(/^,\s*/, "", func_part)
            if (func_part != "") {
                print func_part
            }
        }
        $0 = substr($0, RSTART + RLENGTH)
    }
    
    $0 = $0
    
    # Find DAP_MOCK_WRAPPER_DEFAULT_VOID(func_name, ...)
    # func_name is the first argument
    while (match($0, /DAP_MOCK_WRAPPER_DEFAULT_VOID\s*\(\s*[a-zA-Z_][a-zA-Z0-9_]*/)) {
        matched = substr($0, RSTART, RLENGTH)
        # Extract function name after opening paren
        if (match(matched, /\(\s*[a-zA-Z_][a-zA-Z0-9_]*/)) {
            func_part = substr(matched, RSTART, RLENGTH)
            gsub(/^\(\s*/, "", func_part)
            if (func_part != "") {
                print func_part
            }
        }
        $0 = substr($0, RSTART + RLENGTH)
    }
    
    $0 = $0
    
    # Find DAP_MOCK_WRAPPER_PASSTHROUGH(return_type, func_name, ...)
    while (match($0, /DAP_MOCK_WRAPPER_PASSTHROUGH\s*\([^,]*,\s*[a-zA-Z_][a-zA-Z0-9_]*/)) {
        matched = substr($0, RSTART, RLENGTH)
        if (match(matched, /,\s*[a-zA-Z_][a-zA-Z0-9_]*/)) {
            func_part = substr(matched, RSTART, RLENGTH)
            gsub(/^,\s*/, "", func_part)
            if (func_part != "") {
                print func_part
            }
        }
        $0 = substr($0, RSTART + RLENGTH)
    }
    
    $0 = $0
    
    # Find DAP_MOCK_WRAPPER_PASSTHROUGH_VOID(func_name, ...)
    while (match($0, /DAP_MOCK_WRAPPER_PASSTHROUGH_VOID\s*\(\s*[a-zA-Z_][a-zA-Z0-9_]*/)) {
        matched = substr($0, RSTART, RLENGTH)
        if (match(matched, /\(\s*[a-zA-Z_][a-zA-Z0-9_]*/)) {
            func_part = substr(matched, RSTART, RLENGTH)
            gsub(/^\(\s*/, "", func_part)
            if (func_part != "") {
                print func_part
            }
        }
        $0 = substr($0, RSTART + RLENGTH)
    }
    
    $0 = $0
    
    # Find explicit __wrap_ definitions
    while (match($0, /__wrap_[a-zA-Z_][a-zA-Z0-9_]*/)) {
        matched = substr($0, RSTART, RLENGTH)
        # Remove __wrap_ prefix
        gsub(/^__wrap_/, "", matched)
        if (matched != "") {
            print matched
        }
        $0 = substr($0, RSTART + RLENGTH)
    }
}

