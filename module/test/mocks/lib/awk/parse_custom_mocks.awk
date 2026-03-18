# Parse DAP_MOCK_WRAPPER_CUSTOM declarations and extract:
# - return_type (original, with *)
# - func_name
# - parameters list (type and name from PARAM(...) or void)
# - param_count (number of parameters)
# Output format: return_type|func_name|param_list|macro_type|param_count

# Function to count parameters in param_list
# Input: "type1 name1, type2 name2" or "void"
# Returns: number of parameters (0 for void)
function count_params(param_list) {
    if (param_list == "" || param_list == "void") {
        return 0
    }
    # Count commas + 1 = number of parameters
    n = gsub(/,/, ",", param_list)
    return n + 1
}

# Function to process PARAM(type, name) macros into "type name" format
function process_params(params_str) {
    # Remove all newlines and normalize whitespace
    gsub(/\n/, " ", params_str)
    gsub(/[ \t]+/, " ", params_str)
    gsub(/^[ \t]+|[ \t]+$/, "", params_str)
    
    # Extract all PARAM(...) entries
    result = ""
    while (match(params_str, /PARAM[ \t]*\([ \t]*[^,]+[ \t]*,[ \t]*[^)]+[ \t]*\)/)) {
        # Extract type and name from PARAM(type, name)
        param_content = substr(params_str, RSTART, RLENGTH)
        
        # Extract the captured groups manually
        if (match(param_content, /PARAM[ \t]*\([ \t]*[^,]+[ \t]*,[ \t]*[^)]+[ \t]*\)/)) {
            # Get everything inside PARAM(...)
            inner = param_content
            gsub(/^PARAM[ \t]*\([ \t]*/, "", inner)
            gsub(/[ \t]*\)[ \t]*$/, "", inner)
            
            # Split by comma to get type and name
            comma_pos = index(inner, ",")
            if (comma_pos > 0) {
                type = substr(inner, 1, comma_pos - 1)
                name = substr(inner, comma_pos + 1)
                gsub(/^[ \t]+|[ \t]+$/, "", type)
                gsub(/^[ \t]+|[ \t]+$/, "", name)
                
                # Append "type name" to result
                if (result == "") {
                    result = type " " name
                } else {
                    result = result ", " type " " name
                }
            }
        }
        
        # Remove processed PARAM from params_str
        params_str = substr(params_str, RSTART + RLENGTH)
        gsub(/^[ \t,]+/, "", params_str)
    }
    
    # If no PARAM found, return original (might be regular params or void)
    if (result == "") {
        # Remove outer parentheses if present
        gsub(/^\(\s*/, "", params_str)
        gsub(/\s*\)$/, "", params_str)
        gsub(/^[ \t]+|[ \t]+$/, "", params_str)
        return params_str
    }
    
    return result
}

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
/(^|[^a-zA-Z0-9_])(DAP_MOCK_CUSTOM|DAP_MOCK_WRAPPER_CUSTOM|_DAP_MOCK_WRAPPER_CUSTOM_NONVOID|_DAP_MOCK_WRAPPER_CUSTOM_VOID)($|[^a-zA-Z0-9_])/ {
    in_custom = 1
    paren_level = 0
    found_opening_paren = 0
    return_type = ""
    func_name = ""
    params = ""
    param_list = ""
    is_void = 0
    macro_type = ""
    
    if (match($0, /(^|[^a-zA-Z0-9_])DAP_MOCK_CUSTOM\s*\(/)) {
        macro_type = "CUSTOM"
    } else if (match($0, /(^|[^a-zA-Z0-9_])DAP_MOCK_WRAPPER_CUSTOM\s*\(/)) {
        macro_type = "CUSTOM"
    } else if (match($0, /(^|[^a-zA-Z0-9_])_DAP_MOCK_WRAPPER_CUSTOM_NONVOID\s*\(/)) {
        macro_type = "NONVOID"
    } else if (match($0, /(^|[^a-zA-Z0-9_])_DAP_MOCK_WRAPPER_CUSTOM_VOID\s*\(/)) {
        macro_type = "VOID"
    }
    
    if (match($0, /(DAP_MOCK_CUSTOM|DAP_MOCK_WRAPPER_CUSTOM|_DAP_MOCK_WRAPPER_CUSTOM_NONVOID|_DAP_MOCK_WRAPPER_CUSTOM_VOID)\s*\(/)) {
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
            # Output immediately for void (0 params)
            printf "%s|%s|%s|%s|0\n", return_type, func_name, param_list, macro_type
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
                        } else {
                            # Process PARAM(...) macros
                            param_list = process_params(param_list)
                        }
                        # Output: return_type|func_name|param_list|macro_type|param_count
                        printf "%s|%s|%s|%s|%d\n", return_type, func_name, param_list, macro_type, count_params(param_list)
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
                } else {
                    # Process PARAM(...) macros
                    param_list = process_params(param_list)
                }
                # Output: return_type|func_name|param_list|macro_type|param_count
                printf "%s|%s|%s|%s|%d\n", return_type, func_name, param_list, macro_type, count_params(param_list)
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
        } else {
            # Process PARAM(...) macros
            param_list = process_params(param_list)
        }
        # Output: return_type|func_name|param_list|macro_type|param_count
        printf "%s|%s|%s|%s|%d\n", return_type, func_name, param_list, macro_type, count_params(param_list)
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

