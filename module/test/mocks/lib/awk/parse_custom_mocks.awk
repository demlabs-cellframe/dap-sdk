# Parse DAP_MOCK_WRAPPER_CUSTOM declarations and extract:
# - return_type (original, with *)
# - func_name
# - parameters list (type and name from PARAM(...) or void)
# Output format: return_type|func_name|param_list|macro_type
BEGIN {
    in_custom = 0
    paren_level = 0
    found_opening_paren = 0
    return_type = ""
    func_name = ""
    params = ""
    param_list = ""
    is_void = 0
    macro_type = ""  # "CUSTOM", "NONVOID", "VOID"
}
/DAP_MOCK_WRAPPER_CUSTOM|_DAP_MOCK_WRAPPER_CUSTOM_NONVOID|_DAP_MOCK_WRAPPER_CUSTOM_VOID/ {
    in_custom = 1
    paren_level = 0
    found_opening_paren = 0
    return_type = ""
    func_name = ""
    params = ""
    param_list = ""
    is_void = 0
    macro_type = ""
    
    if (match($0, /DAP_MOCK_WRAPPER_CUSTOM\s*\(/)) {
        macro_type = "CUSTOM"
    } else if (match($0, /_DAP_MOCK_WRAPPER_CUSTOM_NONVOID\s*\(/)) {
        macro_type = "NONVOID"
    } else if (match($0, /_DAP_MOCK_WRAPPER_CUSTOM_VOID\s*\(/)) {
        macro_type = "VOID"
    }
    
    if (match($0, /(DAP_MOCK_WRAPPER_CUSTOM|_DAP_MOCK_WRAPPER_CUSTOM_NONVOID|_DAP_MOCK_WRAPPER_CUSTOM_VOID)\s*\(/)) {
        found_opening_paren = 1
        paren_level = 1
        rest = substr($0, RSTART + RLENGTH)
        
        # Extract return_type and func_name
        if (macro_type == "CUSTOM" || macro_type == "NONVOID") {
            # Extract return_type - everything up to first comma
            if (match(rest, /^([^,]+),/)) {
                return_type = substr(rest, RSTART, RLENGTH - 1)
                gsub(/^[ \t]+|[ \t]+$/, "", return_type)
                rest = substr(rest, RSTART + RLENGTH)
            }
            # Extract func_name - next token up to comma or closing paren
            if (match(rest, /^[ \t]*([a-zA-Z_][a-zA-Z0-9_]*)/)) {
                func_name = substr(rest, RSTART, RLENGTH)
                gsub(/^[ \t]+|[ \t]+$/, "", func_name)
                rest = substr(rest, RSTART + RLENGTH)
            }
        } else if (macro_type == "VOID") {
            return_type = "void"
            # Extract func_name - first token
            if (match(rest, /^[ \t]*([a-zA-Z_][a-zA-Z0-9_]*)/)) {
                func_name = substr(rest, RSTART, RLENGTH)
                gsub(/^[ \t]+|[ \t]+$/, "", func_name)
                rest = substr(rest, RSTART + RLENGTH)
            }
        }
        
        # Check if parameters are "void" or empty
        gsub(/^[ \t,]+|[ \t,]+$/, "", rest)
        if (rest == "void") {
            is_void = 1
            param_list = "void"
            # Output immediately for void
            printf "%s|%s|%s|%s\n", return_type, func_name, param_list, macro_type
            in_custom = 0
            next
        } else if (rest == "") {
            # Parameters are on next lines, start collecting
            param_list = ""
        } else {
            # Extract parameters from rest of line
            param_list = rest
            
            # Check for closing paren on same line
            # Start with paren_count = 1 (opening paren of macro)
            paren_count = 1
            for (i = 1; i <= length(param_list); i++) {
                char = substr(param_list, i, 1)
                if (char == "(") paren_count++
                if (char == ")") {
                    paren_count--
                    if (paren_count == 0) {
                        # Found closing paren - extract parameters
                        param_list = substr(param_list, 1, i - 1)
                        gsub(/^[ \t\n,]+|[ \t\n,]+$/, "", param_list)
                        if (param_list == "" || param_list == "void") {
                            param_list = "void"
                        }
                        # Output: return_type|func_name|param_list|macro_type
                        printf "%s|%s|%s|%s\n", return_type, func_name, param_list, macro_type
                        in_custom = 0
                        next
                    }
                }
            }
        }
    }
    next
}
in_custom {
    # Collect parameters from multi-line declarations
    line = $0
    
    # Append line to param_list (preserve newlines as spaces)
    if (param_list == "") {
        param_list = line
    } else {
        param_list = param_list " " line
    }
    
    # Count parentheses to find closing paren of macro
    # Start with paren_count = 1 (opening paren of macro)
    paren_count = 1
    for (i = 1; i <= length(param_list); i++) {
        char = substr(param_list, i, 1)
        if (char == "(") paren_count++
        if (char == ")") {
            paren_count--
            if (paren_count == 0) {
                # Found closing paren - extract parameters
                param_list = substr(param_list, 1, i - 1)
                gsub(/^[ \t\n]+|[ \t\n]+$/, "", param_list)
                # Remove leading comma and whitespace if present
                gsub(/^[ \t\n,]+/, "", param_list)
                if (param_list == "" || param_list == "void") {
                    param_list = "void"
                }
                # Output: return_type|func_name|param_list|macro_type
                printf "%s|%s|%s|%s\n", return_type, func_name, param_list, macro_type
                in_custom = 0
                paren_level = 0
                found_opening_paren = 0
                return_type = ""
                func_name = ""
                params = ""
                param_list = ""
                is_void = 0
                macro_type = ""
                next
            }
        }
    }
    
    # Check for opening brace (end of macro, no more parameters)
    if (match(line, /\{/)) {
        # Extract everything before opening brace
        gsub(/\{.*$/, "", param_list)
        gsub(/^[ \t\n]+|[ \t\n]+$/, "", param_list)
        # Remove leading comma and whitespace if present
        gsub(/^[ \t\n,]+/, "", param_list)
        if (param_list == "" || param_list == "void") {
            param_list = "void"
        }
        # Output: return_type|func_name|param_list|macro_type
        printf "%s|%s|%s|%s\n", return_type, func_name, param_list, macro_type
        in_custom = 0
        paren_level = 0
        found_opening_paren = 0
        return_type = ""
        func_name = ""
        params = ""
        param_list = ""
        is_void = 0
        macro_type = ""
        next
    }
}

