# Extract return types (normalized and original) from DAP_MOCK_WRAPPER_CUSTOM declarations
# Output format: normalized_type|original_type (one per declaration)
BEGIN {
    in_custom = 0
    paren_level = 0
    found_opening_paren = 0
    return_type = ""
    return_type_original = ""
}
/DAP_MOCK_WRAPPER_CUSTOM|_DAP_MOCK_WRAPPER_CUSTOM_NONVOID|_DAP_MOCK_WRAPPER_CUSTOM_VOID/ {
    in_custom = 1
    paren_level = 0
    found_opening_paren = 0
    return_type = ""
    return_type_original = ""
    
    if (match($0, /(DAP_MOCK_WRAPPER_CUSTOM|_DAP_MOCK_WRAPPER_CUSTOM_NONVOID|_DAP_MOCK_WRAPPER_CUSTOM_VOID)\s*\(/)) {
        found_opening_paren = 1
        paren_level = 1
        rest = substr($0, RSTART + RLENGTH)
        if (match($0, /DAP_MOCK_WRAPPER_CUSTOM\s*\(/)) {
            if (match(rest, /^([^,)]+)/)) {
                return_type_original = substr(rest, RSTART, RLENGTH)
                gsub(/^[ \t]+|[ \t]+$/, "", return_type_original)
                return_type = return_type_original
                gsub(/\*/, "_PTR", return_type)
                gsub(/[ \t]+/, "_", return_type)
                gsub(/_+$/, "", return_type)
                # Output both normalized and original (for normalization macros)
                print return_type "|" return_type_original
            }
        } else if (match($0, /_DAP_MOCK_WRAPPER_CUSTOM_NONVOID\s*\(/)) {
            if (match(rest, /^([^,]+)/)) {
                return_type_original = substr(rest, RSTART, RLENGTH)
                gsub(/^[ \t]+|[ \t]+$/, "", return_type_original)
                return_type = return_type_original
                gsub(/\*/, "_PTR", return_type)
                gsub(/[ \t]+/, "_", return_type)
                gsub(/_+$/, "", return_type)
                print return_type "|" return_type_original
            }
        } else if (match($0, /_DAP_MOCK_WRAPPER_CUSTOM_VOID\s*\(/)) {
            return_type = "void"
            return_type_original = "void"
            print return_type "|" return_type_original
        }
        
        while (match(rest, /[()]/)) {
            char = substr(rest, RSTART, 1)
            if (char == "(") paren_level++
            if (char == ")") {
                paren_level--
                if (paren_level == 0) {
                    if (return_type != "") print return_type "|" return_type_original
                    in_custom = 0
                    return_type = ""
                    return_type_original = ""
                    next
                }
            }
            rest = substr(rest, RSTART + RLENGTH)
        }
    }
    next
}
in_custom {
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
                if (return_type != "") print return_type "|" return_type_original
                in_custom = 0
                return_type = ""
                return_type_original = ""
                next
            }
        }
        if (char == "{" && found_opening_paren && paren_level == 0) {
            if (return_type != "") print return_type "|" return_type_original
            in_custom = 0
            return_type = ""
            return_type_original = ""
            next
        }
    }
}

