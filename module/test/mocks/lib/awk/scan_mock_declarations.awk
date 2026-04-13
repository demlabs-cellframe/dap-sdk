#!/usr/bin/awk -f
# Scan source files for DAP_MOCK_DECLARE and DAP_MOCK_DECLARE_CUSTOM declarations
# Output: newline-separated list of function names
# POSIX awk compatible

{
    line = $0
    
    # Find DAP_MOCK_DECLARE(func_name) or DAP_MOCK_DECLARE(func_name, ...)
    while (match(line, /DAP_MOCK_DECLARE[ \t]*\([ \t]*[a-zA-Z_][a-zA-Z0-9_]*/)) {
        # Extract the matched portion
        matched = substr(line, RSTART, RLENGTH)
        # Remove "DAP_MOCK_DECLARE" and opening paren and spaces
        gsub(/^DAP_MOCK_DECLARE[ \t]*\([ \t]*/, "", matched)
        if (matched != "") {
            print matched
        }
        # Continue searching
        line = substr(line, RSTART + RLENGTH)
    }
    
    line = $0
    
    # Find DAP_MOCK_DECLARE_CUSTOM(func_name, ...)
    while (match(line, /DAP_MOCK_DECLARE_CUSTOM[ \t]*\([ \t]*[a-zA-Z_][a-zA-Z0-9_]*/)) {
        matched = substr(line, RSTART, RLENGTH)
        gsub(/^DAP_MOCK_DECLARE_CUSTOM[ \t]*\([ \t]*/, "", matched)
        if (matched != "") {
            print matched
        }
        line = substr(line, RSTART + RLENGTH)
    }
    
    line = $0
    
    # Find DAP_MOCK_CUSTOM(return_type, func_name, ...)
    while (match(line, /DAP_MOCK_CUSTOM[ \t]*\([^,]+,[ \t]*[a-zA-Z_][a-zA-Z0-9_]*/)) {
        matched = substr(line, RSTART, RLENGTH)
        # Remove everything up to and including the comma
        sub(/^DAP_MOCK_CUSTOM[ \t]*\([^,]+,[ \t]*/, "", matched)
        if (matched != "") {
            print matched
        }
        line = substr(line, RSTART + RLENGTH)
    }
}
