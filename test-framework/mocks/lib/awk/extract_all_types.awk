# Extract all types used in DAP_MOCK_WRAPPER_CUSTOM declarations
# Collects types from:
# 1. Return types (already in RETURN_TYPES)
# 2. Parameter types in PARAM(...) declarations
# Output format: normalized_type|original_type (one per type found)
BEGIN {
    in_custom = 0
    paren_level = 0
    found_opening_paren = 0
    return_type = ""
    return_type_original = ""
    types_seen[""] = 1  # Track seen types to avoid duplicates
}

# Match DAP_MOCK_WRAPPER_CUSTOM declarations
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
        
        # Extract return type
        if (match($0, /DAP_MOCK_WRAPPER_CUSTOM\s*\(/)) {
            if (match(rest, /^([^,)]+)/)) {
                return_type_original = substr(rest, RSTART, RLENGTH)
                gsub(/^[ \t]+|[ \t]+$/, "", return_type_original)
                return_type = normalize_type(return_type_original)
                if (return_type != "" && !(return_type "|" return_type_original in types_seen)) {
                    types_seen[return_type "|" return_type_original] = 1
                    print return_type "|" return_type_original
                }
            }
        } else if (match($0, /_DAP_MOCK_WRAPPER_CUSTOM_NONVOID\s*\(/)) {
            if (match(rest, /^([^,]+)/)) {
                return_type_original = substr(rest, RSTART, RLENGTH)
                gsub(/^[ \t]+|[ \t]+$/, "", return_type_original)
                return_type = normalize_type(return_type_original)
                if (return_type != "" && !(return_type "|" return_type_original in types_seen)) {
                    types_seen[return_type "|" return_type_original] = 1
                    print return_type "|" return_type_original
                }
            }
        } else if (match($0, /_DAP_MOCK_WRAPPER_CUSTOM_VOID\s*\(/)) {
            return_type = "void"
            return_type_original = "void"
            if (!(return_type "|" return_type_original in types_seen)) {
                types_seen[return_type "|" return_type_original] = 1
                print return_type "|" return_type_original
            }
        }
    }
    next
}

# Process PARAM(...) declarations to extract parameter types
/PARAM\s*\(/ {
    if (in_custom || match($0, /DAP_MOCK_WRAPPER_CUSTOM/)) {
        # Extract type from PARAM(type, name)
        if (match($0, /PARAM\s*\(\s*([^,)]+)/)) {
            param_type_original = substr($0, RSTART + 6, RLENGTH - 6)
            gsub(/^[ \t]+|[ \t]+$/, "", param_type_original)
            # Remove const, volatile qualifiers for normalization
            gsub(/^(const|volatile|const volatile|volatile const)[ \t]+/, "", param_type_original)
            gsub(/[ \t]+(const|volatile|const volatile|volatile const)$/, "", param_type_original)
            param_type = normalize_type(param_type_original)
            if (param_type != "" && !(param_type "|" param_type_original in types_seen)) {
                types_seen[param_type "|" param_type_original] = 1
                print param_type "|" param_type_original
            }
        }
    }
    next
}

# Normalize type name: convert pointer types, remove spaces
function normalize_type(type_str) {
    result = type_str
    # Remove leading/trailing whitespace
    gsub(/^[ \t]+|[ \t]+$/, "", result)
    if (result == "") return ""
    
    # Convert pointer types: type* -> type_PTR
    gsub(/\*/, "_PTR", result)
    # Replace spaces and multiple underscores with single underscore
    gsub(/[ \t]+/, "_", result)
    gsub(/_+/, "_", result)
    # Remove trailing underscores
    gsub(/_+$/, "", result)
    
    return result
}

