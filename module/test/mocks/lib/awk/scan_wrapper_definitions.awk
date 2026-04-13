#!/usr/bin/awk -f
# Scan source files for wrapper definitions
# Output: newline-separated list of function names that have wrappers
# MAWK COMPATIBLE - no capture groups

{
    # Find DAP_MOCK_CUSTOM declarations
    # These create __wrap_ functions via DAP_MOCK_WRAPPER_CUSTOM expansion
    if (match($0, /(^|[^a-zA-Z0-9_])DAP_MOCK_CUSTOM\s*\(/)) {
        rest = substr($0, RSTART + RLENGTH)
        # Skip return type (first arg before comma)
        if (match(rest, /^[^,]+,/)) {
            rest = substr(rest, RSTART + RLENGTH)
        }
        # Extract function name (second arg)
        gsub(/^[ \t]+/, "", rest)
        if (match(rest, /^[a-zA-Z_][a-zA-Z0-9_]*/)) {
            func_name = substr(rest, RSTART, RLENGTH)
            print func_name
        }
    }
    
    # Find DAP_MOCK_WRAPPER_CUSTOM declarations
    # These create __wrap_ functions, so we should skip generating headers for them
    if (match($0, /(^|[^a-zA-Z0-9_])DAP_MOCK_WRAPPER_CUSTOM\s*\(/)) {
        rest = substr($0, RSTART + RLENGTH)
        # Skip return type (first arg before comma)
        if (match(rest, /^[^,]+,/)) {
            rest = substr(rest, RSTART + RLENGTH)
        }
        # Extract function name (second arg)
        gsub(/^[ \t]+/, "", rest)
        if (match(rest, /^[a-zA-Z_][a-zA-Z0-9_]*/)) {
            func_name = substr(rest, RSTART, RLENGTH)
            print func_name
        }
    }
    
    # Find explicit __wrap_ function definitions
    # These indicate that the user has manually implemented a wrapper
    # and we should not generate a duplicate
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

