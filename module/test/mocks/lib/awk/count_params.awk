# Count parameters in DAP_MOCK_WRAPPER_CUSTOM declarations
# Supports two formats:
# 1. PARAM(type, name) - old format: counts PARAM macros
# 2. (type name, type2 name2) - new format: counts commas in parameter list + 1
# Output: param_count (one per declaration)

BEGIN {
    in_custom = 0
    param_count = 0
    paren_level = 0
    found_opening_paren = 0
    has_param_macro = 0
    params_content = ""
    after_func_name = ""
}

/DAP_MOCK_WRAPPER_CUSTOM|_DAP_MOCK_WRAPPER_CUSTOM_NONVOID|_DAP_MOCK_WRAPPER_CUSTOM_VOID/ {
    in_custom = 1
    param_count = 0
    paren_level = 0
    found_opening_paren = 0
    has_param_macro = 0
    params_content = ""
    after_func_name = ""
    
    if (match($0, /(DAP_MOCK_WRAPPER_CUSTOM|_DAP_MOCK_WRAPPER_CUSTOM_NONVOID|_DAP_MOCK_WRAPPER_CUSTOM_VOID)\s*\(/)) {
        found_opening_paren = 1
        paren_level = 1
        rest = substr($0, RSTART + RLENGTH)
        
        # Skip return_type and func_name to get to parameters
        # DAP_MOCK_WRAPPER_CUSTOM(return_type, func_name, PARAMS)
        comma_count = 0
        temp_rest = rest
        for (i = 1; i <= length(temp_rest); i++) {
            char = substr(temp_rest, i, 1)
            if (char == ",") {
                comma_count++
                if (comma_count == 2) {
                    # Everything after second comma is parameters
                    after_func_name = substr(temp_rest, i + 1)
                    break
                }
            }
        }
        
        # Check which format: PARAM(...) or (...)
        if (match(after_func_name, /PARAM\s*\(/)) {
            has_param_macro = 1
        }
        
        # Collect parameters content
        params_content = after_func_name
        
        # Check for closing paren on same line
        while (match(rest, /[()]/)) {
            char = substr(rest, RSTART, 1)
            if (char == "(") paren_level++
            if (char == ")") {
                paren_level--
                if (paren_level == 0) {
                    # Macro closed on same line
                    if (has_param_macro) {
                        param_count = count_param_macros(params_content)
                    } else {
                        param_count = count_params_in_parens(params_content)
                    }
                    print param_count
                    in_custom = 0
                    next
                }
            }
            rest = substr(rest, RSTART + RLENGTH)
        }
    }
    next
}

in_custom {
    # Accumulate parameters content
    params_content = params_content "\n" $0
    
    # Track parentheses
    for (i = 1; i <= length($0); i++) {
        char = substr($0, i, 1)
        
        if (char == "(") {
            paren_level++
        }
        
        if (char == ")") {
            paren_level--
            if (paren_level <= 0 && found_opening_paren) {
                # Macro closed
                if (!has_param_macro) {
                    # Check if PARAM appeared in collected content
                    if (match(params_content, /PARAM\s*\(/)) {
                        has_param_macro = 1
                    }
                }
                
                if (has_param_macro) {
                    param_count = count_param_macros(params_content)
                } else {
                    param_count = count_params_in_parens(params_content)
                }
                
                print param_count
                in_custom = 0
                param_count = 0
                paren_level = 0
                found_opening_paren = 0
                has_param_macro = 0
                params_content = ""
                next
            }
        }
        
        if (char == "{" && found_opening_paren && paren_level == 0) {
            # Found opening brace
            if (!has_param_macro) {
                # Check if PARAM appeared
                if (match(params_content, /PARAM\s*\(/)) {
                    has_param_macro = 1
                }
            }
            
            if (has_param_macro) {
                param_count = count_param_macros(params_content)
            } else {
                param_count = count_params_in_parens(params_content)
            }
            
            print param_count
            in_custom = 0
            param_count = 0
            paren_level = 0
            found_opening_paren = 0
            has_param_macro = 0
            params_content = ""
            next
        }
    }
}

# Count PARAM(...) macros
function count_param_macros(text,    count, temp) {
    count = 0
    temp = text
    while (match(temp, /PARAM\s*\(/)) {
        count++
        temp = substr(temp, RSTART + RLENGTH)
    }
    return count
}

# Count parameters in parentheses format: (type name, type2 name2, ...)
function count_params_in_parens(text,    cleaned, i, char, depth, comma_count) {
    # Remove newlines and normalize whitespace
    gsub(/\n/, " ", text)
    gsub(/[ \t]+/, " ", text)
    gsub(/^[ \t]+|[ \t]+$/, "", text)
    
    # Extract content between first ( and matching )
    if (match(text, /\(/)) {
        text = substr(text, RSTART + 1)
        
        # Find matching closing paren
        depth = 1
        for (i = 1; i <= length(text); i++) {
            char = substr(text, i, 1)
            if (char == "(") depth++
            if (char == ")") {
                depth--
                if (depth == 0) {
                    text = substr(text, 1, i - 1)
                    break
                }
            }
        }
    }
    
    # Trim again
    gsub(/^[ \t]+|[ \t]+$/, "", text)
    
    # Check for void or empty
    if (text == "" || text == "void" || text == "void ") {
        return 0
    }
    
    # Count commas at depth 0 (not inside nested parentheses/brackets)
    comma_count = 0
    depth = 0
    
    for (i = 1; i <= length(text); i++) {
        char = substr(text, i, 1)
        if (char == "(" || char == "[" || char == "<") {
            depth++
        } else if (char == ")" || char == "]" || char == ">") {
            depth--
        } else if (char == "," && depth == 0) {
            comma_count++
        }
    }
    
    # Number of params = comma_count + 1
    return comma_count + 1
}
