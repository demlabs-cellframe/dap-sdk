# Count PARAM(...) entries in DAP_MOCK_WRAPPER_CUSTOM declarations
# Output: param_count (one per declaration)
BEGIN {
    in_custom = 0
    param_count = 0
    paren_level = 0
    found_opening_paren = 0
    return_type = ""
}
/DAP_MOCK_WRAPPER_CUSTOM|_DAP_MOCK_WRAPPER_CUSTOM_NONVOID|_DAP_MOCK_WRAPPER_CUSTOM_VOID/ {
    in_custom = 1
    param_count = 0
    paren_level = 0
    found_opening_paren = 0
    return_type = ""
    
    # Check if this line contains opening parenthesis
    # Match any of: DAP_MOCK_WRAPPER_CUSTOM, _DAP_MOCK_WRAPPER_CUSTOM_NONVOID, _DAP_MOCK_WRAPPER_CUSTOM_VOID
    if (match($0, /(DAP_MOCK_WRAPPER_CUSTOM|_DAP_MOCK_WRAPPER_CUSTOM_NONVOID|_DAP_MOCK_WRAPPER_CUSTOM_VOID)\s*\(/)) {
        found_opening_paren = 1
        paren_level = 1  # Opening paren of macro(
        
        # Extract return_type (first argument after macro name)
        # For DAP_MOCK_WRAPPER_CUSTOM(return_type, func_name, ...)
        # For _DAP_MOCK_WRAPPER_CUSTOM_NONVOID(return_type, func_name, ...)
        # For _DAP_MOCK_WRAPPER_CUSTOM_VOID(func_name, ...) - no return_type
        rest = substr($0, RSTART + RLENGTH)
        if (match($0, /DAP_MOCK_WRAPPER_CUSTOM\s*\(/)) {
            # Extract return_type - everything up to first comma or closing paren
            if (match(rest, /^([^,)]+)/)) {
                return_type = substr(rest, RSTART, RLENGTH)
                gsub(/^[ \t]+|[ \t]+$/, "", return_type)  # Trim whitespace
                # Normalize return_type: replace * with _PTR, spaces with _
                gsub(/\*/, "_PTR", return_type)
                gsub(/[ \t]+/, "_", return_type)
                # Remove trailing underscores
                gsub(/_+$/, "", return_type)
            }
        } else if (match($0, /_DAP_MOCK_WRAPPER_CUSTOM_NONVOID\s*\(/)) {
            # Extract return_type - everything up to first comma
            if (match(rest, /^([^,]+)/)) {
                return_type = substr(rest, RSTART, RLENGTH)
                gsub(/^[ \t]+|[ \t]+$/, "", return_type)  # Trim whitespace
                # Normalize return_type: replace * with _PTR, spaces with _
                gsub(/\*/, "_PTR", return_type)
                gsub(/[ \t]+/, "_", return_type)
                # Remove trailing underscores
                gsub(/_+$/, "", return_type)
            }
        } else if (match($0, /_DAP_MOCK_WRAPPER_CUSTOM_VOID\s*\(/)) {
            return_type = "void"
        }
        
        # Count PARAM( entries in rest of line
        while (match(rest, /PARAM\s*\(/)) {
            param_count++
            rest = substr(rest, RSTART + RLENGTH)
        }
        # Check for closing paren on same line
        while (match(rest, /[()]/)) {
            char = substr(rest, RSTART, 1)
            if (char == "(") paren_level++
            if (char == ")") {
                paren_level--
                if (paren_level == 0) {
                    # Found closing paren - output count and return_type
                    print param_count
                    if (return_type != "") {
                        print return_type
                    }
                    in_custom = 0
                    param_count = 0
                    paren_level = 0
                    found_opening_paren = 0
                    return_type = ""
                    next
                }
            }
            rest = substr(rest, RSTART + RLENGTH)
        }
    }
    next
}
in_custom {
    # Count parentheses to track when we exit the macro parameter list
    for (i = 1; i <= length($0); i++) {
        char = substr($0, i, 1)
        if (char == "(") {
            paren_level++
            if (!found_opening_paren) {
                found_opening_paren = 1
                paren_level = 1
            }
        }
        if (char == ")") {
            paren_level--
            if (paren_level <= 0 && found_opening_paren) {
                # We found the closing parenthesis of macro
                # Output param count
                print param_count
                in_custom = 0
                param_count = 0
                paren_level = 0
                found_opening_paren = 0
                next
            }
        }
        if (char == "{" && found_opening_paren && paren_level == 0) {
            # We found opening brace - this means no parameters (or already closed)
            # Output param count
            print param_count
            in_custom = 0
            param_count = 0
            paren_level = 0
            found_opening_paren = 0
            next
        }
    }
    
    # Count PARAM( entries in current line
    line = $0
    while (match(line, /PARAM\s*\(/)) {
        param_count++
        line = substr(line, RSTART + RLENGTH)
    }
}

